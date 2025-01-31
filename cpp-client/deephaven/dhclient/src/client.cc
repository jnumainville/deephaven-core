/*
 * Copyright (c) 2016-2023 Deephaven Data Labs and Patent Pending
 */
#include "deephaven/client/client.h"

#include <grpc/support/log.h>
#include <arrow/array.h>
#include <arrow/scalar.h>
#include "deephaven/client/columns.h"
#include "deephaven/client/flight.h"
#include "deephaven/client/impl/columns_impl.h"
#include "deephaven/client/impl/boolean_expression_impl.h"
#include "deephaven/client/impl/aggregate_impl.h"
#include "deephaven/client/impl/client_impl.h"
#include "deephaven/client/impl/table_handle_impl.h"
#include "deephaven/client/impl/table_handle_manager_impl.h"
#include "deephaven/client/impl/update_by_operation_impl.h"
#include "deephaven/client/impl/util.h"
#include "deephaven/client/subscription/subscription_handle.h"
#include "deephaven/client/utility/arrow_util.h"
#include "deephaven/dhcore/clienttable/schema.h"
#include "deephaven/dhcore/utility/utility.h"
#include "deephaven/proto/table.pb.h"
#include "deephaven/proto/table.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using io::deephaven::proto::backplane::grpc::ComboAggregateRequest;
using io::deephaven::proto::backplane::grpc::HandshakeRequest;
using io::deephaven::proto::backplane::grpc::HandshakeResponse;
using io::deephaven::proto::backplane::grpc::Ticket;
using deephaven::client::server::Server;
using deephaven::client::Column;
using deephaven::client::DateTimeCol;
using deephaven::client::NumCol;
using deephaven::client::StrCol;
using deephaven::client::impl::StrColImpl;
using deephaven::client::impl::AggregateComboImpl;
using deephaven::client::impl::AggregateImpl;
using deephaven::client::impl::ClientImpl;
using deephaven::client::impl::MoveVectorData;
using deephaven::client::impl::UpdateByOperationImpl;
using deephaven::client::subscription::SubscriptionHandle;
using deephaven::client::utility::Executor;
using deephaven::client::utility::OkOrThrow;
using deephaven::dhcore::clienttable::Schema;
using deephaven::dhcore::utility::Base64Encode;
using deephaven::dhcore::utility::MakeReservedVector;
using deephaven::dhcore::utility::separatedList;
using deephaven::dhcore::utility::SFCallback;
using deephaven::dhcore::utility::SimpleOstringstream;
using deephaven::dhcore::utility::Stringf;


namespace deephaven::client {
namespace {
void printTableData(std::ostream &s, const TableHandle &table_handle, bool want_headers);
}  // namespace

Client Client::Connect(const std::string &target, const ClientOptions &options) {
  auto server = Server::CreateFromTarget(target, options);
  auto executor = Executor::Create("Client executor for " + server->me());
  auto flight_executor = Executor::Create("Flight executor for " + server->me());
  auto impl = ClientImpl::Create(std::move(server), executor, flight_executor, options.sessionType_);
  return Client(std::move(impl));
}

Client::Client() = default;

Client::Client(std::shared_ptr<impl::ClientImpl> impl) : impl_(std::move(impl)) {
}
Client::Client(Client &&other) noexcept = default;
Client &Client::operator=(Client &&other) noexcept = default;

// There is only one Client associated with the server connection. Clients can only be moved, not
// copied. When the Client owning the state is destructed, we tear down the state via close().
Client::~Client() {
  Close();
}

// Tear down Client state.
void Client::Close() {
  // Move to local variable to be defensive.
  auto temp = std::move(impl_);
  if (temp != nullptr) {
    temp->Shutdown();
  }
}

TableHandleManager Client::GetManager() const {
  return TableHandleManager(impl_->ManagerImpl());
}


TableHandleManager::TableHandleManager() = default;
TableHandleManager::TableHandleManager(std::shared_ptr<impl::TableHandleManagerImpl> impl) : impl_(std::move(impl)) {}
TableHandleManager::TableHandleManager(TableHandleManager &&other) noexcept = default;
TableHandleManager &TableHandleManager::operator=(TableHandleManager &&other) noexcept = default;
TableHandleManager::~TableHandleManager() = default;

TableHandle TableHandleManager::EmptyTable(int64_t size) const {
  auto qs_impl = impl_->EmptyTable(size);
  return TableHandle(std::move(qs_impl));
}

TableHandle TableHandleManager::FetchTable(std::string tableName) const {
  auto qs_impl = impl_->FetchTable(std::move(tableName));
  return TableHandle(std::move(qs_impl));
}

TableHandle TableHandleManager::TimeTable(DurationSpecifier period, TimePointSpecifier start_time,
    bool blink_table) const {
  auto impl = impl_->TimeTable(std::move(period), std::move(start_time), blink_table);
  return TableHandle(std::move(impl));
}

std::string TableHandleManager::NewTicket() const {
  return impl_->NewTicket();
}

TableHandle TableHandleManager::MakeTableHandleFromTicket(std::string ticket) const {
  auto handle_impl = impl_->MakeTableHandleFromTicket(std::move(ticket));
  return TableHandle(std::move(handle_impl));
}

void TableHandleManager::RunScript(std::string code) const {
  auto res = SFCallback<>::CreateForFuture();
  impl_->RunScriptAsync(std::move(code), std::move(res.first));
  (void)res.second.get();
}

namespace {
ComboAggregateRequest::Aggregate
createDescForMatchPairs(ComboAggregateRequest::AggType aggregate_type,
    std::vector<std::string> column_specs) {
  ComboAggregateRequest::Aggregate result;
  result.set_type(aggregate_type);
  for (auto &cs : column_specs) {
    result.mutable_match_pairs()->Add(std::move(cs));
  }
  return result;
}

ComboAggregateRequest::Aggregate CreateDescForColumn(ComboAggregateRequest::AggType aggregate_type,
    std::string column_spec) {
  ComboAggregateRequest::Aggregate result;
  result.set_type(aggregate_type);
  result.set_column_name(std::move(column_spec));
  return result;
}

Aggregate createAggForMatchPairs(ComboAggregateRequest::AggType aggregate_type, std::vector<std::string> column_specs) {
  auto ad = createDescForMatchPairs(aggregate_type, std::move(column_specs));
  auto impl = AggregateImpl::Create(std::move(ad));
  return Aggregate(std::move(impl));
}
}  // namespace

Aggregate::Aggregate(std::shared_ptr<impl::AggregateImpl> impl) : impl_(std::move(impl)) {
}

Aggregate Aggregate::AbsSum(std::vector<std::string> columnSpecs) {
  return createAggForMatchPairs(ComboAggregateRequest::ABS_SUM, std::move(columnSpecs));
}

Aggregate Aggregate::Avg(std::vector<std::string> column_specs) {
  return createAggForMatchPairs(ComboAggregateRequest::AVG, std::move(column_specs));
}

Aggregate Aggregate::Count(std::string column_spec) {
  auto ad = CreateDescForColumn(ComboAggregateRequest::COUNT, std::move(column_spec));
  auto impl = AggregateImpl::Create(std::move(ad));
  return Aggregate(std::move(impl));
}

Aggregate Aggregate::First(std::vector<std::string> column_specs) {
  return createAggForMatchPairs(ComboAggregateRequest::FIRST, std::move(column_specs));
}

Aggregate Aggregate::Last(std::vector<std::string> column_specs) {
  return createAggForMatchPairs(ComboAggregateRequest::LAST, std::move(column_specs));
}

Aggregate Aggregate::Max(std::vector<std::string> columnSpecs) {
  return createAggForMatchPairs(ComboAggregateRequest::MAX, std::move(columnSpecs));
}

Aggregate Aggregate::Med(std::vector<std::string> column_specs) {
  return createAggForMatchPairs(ComboAggregateRequest::MEDIAN, std::move(column_specs));
}

Aggregate Aggregate::Min(std::vector<std::string> column_specs) {
  return createAggForMatchPairs(ComboAggregateRequest::MIN, std::move(column_specs));
}

Aggregate Aggregate::Pct(double percentile, bool avg_median, std::vector<std::string> column_specs) {
  ComboAggregateRequest::Aggregate pd;
  pd.set_type(ComboAggregateRequest::PERCENTILE);
  pd.set_percentile(percentile);
  pd.set_avg_median(avg_median);
  for (auto &cs : column_specs) {
    pd.mutable_match_pairs()->Add(std::move(cs));
  }
  auto impl = AggregateImpl::Create(std::move(pd));
  return Aggregate(std::move(impl));
}

Aggregate Aggregate::Std(std::vector<std::string> column_specs) {
  return createAggForMatchPairs(ComboAggregateRequest::STD, std::move(column_specs));
}

Aggregate Aggregate::Sum(std::vector<std::string> column_specs) {
  return createAggForMatchPairs(ComboAggregateRequest::SUM, std::move(column_specs));
}

Aggregate Aggregate::Var(std::vector<std::string> column_specs) {
  return createAggForMatchPairs(ComboAggregateRequest::VAR, std::move(column_specs));
}

Aggregate Aggregate::WAvg(std::string weight_column, std::vector<std::string> column_specs) {
  ComboAggregateRequest::Aggregate pd;
  pd.set_type(ComboAggregateRequest::WEIGHTED_AVG);
  for (auto &cs : column_specs) {
    pd.mutable_match_pairs()->Add(std::move(cs));
  }
  pd.set_column_name(std::move(weight_column));
  auto impl = AggregateImpl::Create(std::move(pd));
  return Aggregate(std::move(impl));
}

AggregateCombo AggregateCombo::Create(std::initializer_list<Aggregate> list) {
  std::vector<ComboAggregateRequest::Aggregate> aggregates;
  aggregates.reserve(list.size());
  for (const auto &item : list) {
    aggregates.push_back(item.Impl()->Descriptor());
  }
  auto impl = AggregateComboImpl::Create(std::move(aggregates));
  return AggregateCombo(std::move(impl));
}

AggregateCombo AggregateCombo::Create(std::vector<Aggregate> vec) {
  std::vector<ComboAggregateRequest::Aggregate> aggregates;
  aggregates.reserve(vec.size());
  for (auto &item : vec) {
    aggregates.push_back(std::move(item.Impl()->Descriptor()));
  }
  auto impl = AggregateComboImpl::Create(std::move(aggregates));
  return AggregateCombo(std::move(impl));
}

AggregateCombo::AggregateCombo(std::shared_ptr<impl::AggregateComboImpl> impl) : impl_(std::move(impl)) {}
AggregateCombo::AggregateCombo(AggregateCombo &&other) noexcept = default;
AggregateCombo &AggregateCombo::operator=(AggregateCombo &&other) noexcept = default;
AggregateCombo::~AggregateCombo() = default;

TableHandle::TableHandle() = default;
TableHandle::TableHandle(std::shared_ptr<impl::TableHandleImpl> impl) : impl_(std::move(impl)) {
}
TableHandle::TableHandle(const TableHandle &other) = default;
TableHandle &TableHandle::operator=(const TableHandle &other) = default;
TableHandle::TableHandle(TableHandle &&other) noexcept = default;
TableHandle &TableHandle::operator=(TableHandle &&other) noexcept = default;
TableHandle::~TableHandle() = default;

TableHandleManager TableHandle::GetManager() const {
  return TableHandleManager(impl_->ManagerImpl());
}

TableHandle TableHandle::Where(const BooleanExpression &condition) const {
  SimpleOstringstream oss;
    condition.implAsBooleanExpressionImpl()->StreamIrisRepresentation(oss);
  return Where(std::move(oss.str()));
}

TableHandle TableHandle::Where(std::string condition) const {
  auto qt_impl = impl_->Where(std::move(condition));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::Sort(std::vector<SortPair> sortPairs) const {
  auto qt_impl = impl_->Sort(std::move(sortPairs));
  return TableHandle(std::move(qt_impl));
}

std::vector<Column> TableHandle::GetAllCols() const {
  auto column_impls = impl_->GetColumnImpls();
  std::vector<Column> result;
  result.reserve(column_impls.size());
  for (const auto &ci : column_impls) {
    result.emplace_back(ci);
  }
  return result;
}

StrCol TableHandle::GetStrCol(std::string columnName) const {
  auto sc_impl = impl_->GetStrColImpl(std::move(columnName));
  return StrCol(std::move(sc_impl));
}

NumCol TableHandle::GetNumCol(std::string columnName) const {
  auto nc_impl = impl_->GetNumColImpl(std::move(columnName));
  return NumCol(std::move(nc_impl));
}

DateTimeCol TableHandle::GetDateTimeCol(std::string columnName) const {
  auto dt_impl = impl_->GetDateTimeColImpl(std::move(columnName));
  return DateTimeCol(std::move(dt_impl));
}

TableHandle TableHandle::Select(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->Select(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::Update(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->Update(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::View(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->View(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::DropColumns(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->DropColumns(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::UpdateView(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->UpdateView(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::By(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->By(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::By(AggregateCombo combo, std::vector<std::string> groupByColumns) const {
  auto qt_impl = impl_->By(combo.Impl()->Aggregates(), std::move(groupByColumns));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::MinBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->MinBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::MaxBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->MaxBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::SumBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->SumBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::AbsSumBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->AbsSumBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::VarBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->VarBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::StdBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->StdBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::AvgBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->AvgBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::LastBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->LastBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::FirstBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->FirstBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::MedianBy(std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->MedianBy(std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::PercentileBy(double percentile, bool avgMedian,
    std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->PercentileBy(percentile, avgMedian, std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::PercentileBy(double percentile, std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->PercentileBy(percentile, std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::CountBy(std::string countByColumn, std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->CountBy(std::move(countByColumn), std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::WAvgBy(std::string weightColumn, std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->WavgBy(std::move(weightColumn), std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::TailBy(int64_t n, std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->TailBy(n, std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::HeadBy(int64_t n, std::vector<std::string> columnSpecs) const {
  auto qt_impl = impl_->HeadBy(n, std::move(columnSpecs));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::Head(int64_t n) const {
  auto qt_impl = impl_->Head(n);
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::Tail(int64_t n) const {
  auto qt_impl = impl_->Tail(n);
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::Ungroup(bool nullFill, std::vector<std::string> groupByColumns) const {
  auto qt_impl = impl_->Ungroup(nullFill, std::move(groupByColumns));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::Merge(std::string keyColumn, std::vector<TableHandle> sources) const {
  std::vector<Ticket> source_handles;
  source_handles.reserve(sources.size() + 1);
  source_handles.push_back(impl_->Ticket());
  for (const auto &s : sources) {
    source_handles.push_back(s.Impl()->Ticket());
  }
  auto qt_impl = impl_->Merge(std::move(keyColumn), std::move(source_handles));
  return TableHandle(std::move(qt_impl));
}

namespace {
template<typename T>
std::vector<std::string> toIrisRepresentation(const std::vector<T> &items) {
  std::vector<std::string> result;
  result.reserve(items.size());
  for (const auto &ctm : items) {
    SimpleOstringstream oss;
    ctm.GetIrisRepresentableImpl()->StreamIrisRepresentation(oss);
    result.push_back(std::move(oss.str()));
  }
  return result;
}
}  // namespace

TableHandle TableHandle::CrossJoin(const TableHandle &rightSide,
    std::vector<std::string> columnsToMatch, std::vector<std::string> columnsToAdd) const {
  auto qt_impl = impl_->CrossJoin(*rightSide.impl_, std::move(columnsToMatch),
      std::move(columnsToAdd));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::CrossJoin(const TableHandle &right_side,
    std::vector<MatchWithColumn> columns_to_match, std::vector<SelectColumn> columns_to_add) const {
  auto ctm_strings = toIrisRepresentation(columns_to_match);
  auto cta_strings = toIrisRepresentation(columns_to_add);
  return CrossJoin(right_side, std::move(ctm_strings), std::move(cta_strings));
}

TableHandle TableHandle::NaturalJoin(const TableHandle &rightSide,
    std::vector<std::string> columnsToMatch, std::vector<std::string> columnsToAdd) const {
  auto qt_impl = impl_->NaturalJoin(*rightSide.impl_, std::move(columnsToMatch),
      std::move(columnsToAdd));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::NaturalJoin(const TableHandle &rightSide,
    std::vector<MatchWithColumn> columnsToMatch, std::vector<SelectColumn> columnsToAdd) const {
  auto ctm_strings = toIrisRepresentation(columnsToMatch);
  auto cta_strings = toIrisRepresentation(columnsToAdd);
  return NaturalJoin(rightSide, std::move(ctm_strings), std::move(cta_strings));
}

TableHandle TableHandle::ExactJoin(const TableHandle &rightSide,
    std::vector<std::string> columnsToMatch, std::vector<std::string> columnsToAdd) const {
  auto qt_impl = impl_->ExactJoin(*rightSide.impl_, std::move(columnsToMatch),
      std::move(columnsToAdd));
  return TableHandle(std::move(qt_impl));
}

TableHandle TableHandle::ExactJoin(const TableHandle &rightSide,
    std::vector<MatchWithColumn> columnsToMatch, std::vector<SelectColumn> columnsToAdd) const {
  auto ctm_strings = toIrisRepresentation(columnsToMatch);
  auto cta_strings = toIrisRepresentation(columnsToAdd);
  return ExactJoin(rightSide, std::move(ctm_strings), std::move(cta_strings));
}

TableHandle TableHandle::UpdateBy(std::vector<UpdateByOperation> ops, std::vector<std::string> by) const {
  auto op_impls = MakeReservedVector<std::shared_ptr<UpdateByOperationImpl>>(ops.size());
  for (const auto &op : ops) {
    op_impls.push_back(op.impl_);
  }
  auto th_impl = impl_->UpdateBy(std::move(op_impls), std::move(by));
  return TableHandle(std::move(th_impl));
}

void TableHandle::BindToVariable(std::string variable) const {
  auto res = SFCallback<>::CreateForFuture();
  BindToVariableAsync(std::move(variable), std::move(res.first));
  (void)res.second.get();
}

void TableHandle::BindToVariableAsync(std::string variable,
    std::shared_ptr<SFCallback<>> callback) const {
  return impl_->BindToVariableAsync(std::move(variable), std::move(callback));
}

internal::TableHandleStreamAdaptor TableHandle::Stream(bool want_headers) const {
  return {*this, want_headers};
}

void TableHandle::Observe() const {
  impl_->Observe();
}

int64_t TableHandle::NumRows() const {
  return impl_->NumRows();
}

bool TableHandle::IsStatic() const {
  return impl_->IsStatic();
}

std::shared_ptr<Schema> TableHandle::Schema() const {
  return impl_->Schema();
}

std::shared_ptr<arrow::flight::FlightStreamReader> TableHandle::GetFlightStreamReader() const {
  return GetManager().CreateFlightWrapper().GetFlightStreamReader(*this);
}

std::shared_ptr<SubscriptionHandle> TableHandle::Subscribe(
    std::shared_ptr<TickingCallback> callback) {
  return impl_->Subscribe(std::move(callback));
}

std::shared_ptr<SubscriptionHandle>
TableHandle::Subscribe(onTickCallback_t onTick, void *onTickUserData,
    onErrorCallback_t onError, void *onErrorUserData) {
  return impl_->Subscribe(onTick, onTickUserData, onError, onErrorUserData);
}

void TableHandle::Unsubscribe(std::shared_ptr<SubscriptionHandle> callback) {
  impl_->Unsubscribe(std::move(callback));
}

const std::string &TableHandle::GetTicketAsString() const {
  return impl_->Ticket().ticket();
}

std::string TableHandle::ToString(bool wantHeaders) const {
  SimpleOstringstream oss;
  oss << Stream(wantHeaders);
  return std::move(oss.str());
}

namespace internal {
TableHandleStreamAdaptor::TableHandleStreamAdaptor(TableHandle table, bool want_headers) :
    table_(std::move(table)), wantHeaders_(want_headers) {}
TableHandleStreamAdaptor::~TableHandleStreamAdaptor() = default;

std::ostream &operator<<(std::ostream &s, const TableHandleStreamAdaptor &o) {
  printTableData(s, o.table_, o.wantHeaders_);
  return s;
}

std::string ConvertToString::ToString(
    const deephaven::client::SelectColumn &selectColumn) {
  SimpleOstringstream oss;
  selectColumn.GetIrisRepresentableImpl()->StreamIrisRepresentation(oss);
  return std::move(oss.str());
}
}  // namespace internal

namespace {
void printTableData(std::ostream &s, const TableHandle &table_handle, bool want_headers) {
  auto fsr = table_handle.GetFlightStreamReader();

  if (want_headers) {
    auto cols = table_handle.GetAllCols();
    auto stream_name = [](std::ostream &s, const Column &c) {
      s << c.Name();
    };
    s << separatedList(cols.begin(), cols.end(), "\t", stream_name) << '\n';
  }

  while (true) {
    arrow::flight::FlightStreamChunk chunk;
    OkOrThrow(DEEPHAVEN_EXPR_MSG(fsr->Next(&chunk)));
    if (chunk.data == nullptr) {
      break;
    }
    const auto *data = chunk.data.get();
    const auto &columns = data->columns();
    for (int64_t row_num = 0; row_num < data->num_rows(); ++row_num) {
      if (row_num != 0) {
        s << '\n';
      }
      auto stream_array_cell = [row_num](std::ostream &s, const std::shared_ptr<arrow::Array> &a) {
        // This is going to be rather inefficient
        auto rsc = a->GetScalar(row_num);
        const auto &vsc = *rsc.ValueOrDie();
        s << vsc.ToString();
      };
      s << separatedList(columns.begin(), columns.end(), "\t", stream_array_cell);
    }
  }
}
}  // namespace
}  // namespace deephaven::client
