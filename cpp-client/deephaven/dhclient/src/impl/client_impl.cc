/*
 * Copyright (c) 2016-2022 Deephaven Data Labs and Patent Pending
 */
#include "deephaven/client/impl/client_impl.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include "deephaven/client/impl/table_handle_manager_impl.h"
#include "deephaven/dhcore/utility/callbacks.h"

using io::deephaven::proto::backplane::grpc::HandshakeResponse;
using io::deephaven::proto::backplane::grpc::Ticket;
using io::deephaven::proto::backplane::script::grpc::StartConsoleResponse;

using deephaven::client::impl::TableHandleManagerImpl;
using deephaven::client::server::Server;
using deephaven::client::utility::Executor;
using deephaven::dhcore::utility::SFCallback;

namespace deephaven::client {
namespace impl {
std::shared_ptr<ClientImpl> ClientImpl::Create(
    std::shared_ptr<Server> server,
    std::shared_ptr<Executor> executor,
    std::shared_ptr<Executor> flight_executor,
    const std::string &session_type) {
  std::optional<Ticket> consoleTicket;
  if (!session_type.empty()) {
    auto cb = SFCallback<StartConsoleResponse>::CreateForFuture();
    server->StartConsoleAsync(session_type, std::move(cb.first));
    StartConsoleResponse scr = std::move(std::get<0>(cb.second.get()));
    consoleTicket = std::move(*scr.mutable_result_id());
  }

  auto thmi = TableHandleManagerImpl::Create(
          std::move(consoleTicket),
          std::move(server),
          std::move(executor),
          std::move(flight_executor));
  return std::make_shared<ClientImpl>(Private(), std::move(thmi));
}

ClientImpl::ClientImpl(Private, std::shared_ptr<TableHandleManagerImpl> &&manager_impl) :
    managerImpl_(std::move(manager_impl)) {}

ClientImpl::~ClientImpl() = default;
}  // namespace impl
}  // namespace deephaven::client
