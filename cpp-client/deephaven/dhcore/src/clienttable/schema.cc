/*
 * Copyright (c) 2016-2022 Deephaven Data Labs and Patent Pending
 */
#include "deephaven/dhcore/clienttable/schema.h"
#include "deephaven/dhcore/utility/utility.h"

using deephaven::dhcore::utility::Stringf;

namespace deephaven::dhcore::clienttable {
std::shared_ptr<Schema> Schema::Create(std::vector<std::string> names, std::vector<ElementTypeId::Enum> types) {
  if (names.size() != types.size()) {
    auto message = Stringf("Sizes differ: %o vs %o", names.size(), types.size());
    throw std::runtime_error(DEEPHAVEN_DEBUG_MSG(message));
  }
  std::map<std::string_view, size_t, std::less<>> index;
  for (size_t i = 0; i != names.size(); ++i) {
    std::string_view sv_name = names[i];
    auto [ip, inserted] = index.insert(std::make_pair(sv_name, i));
    if (!inserted) {
      auto message = Stringf("Duplicate column name: %o", sv_name);
      throw std::runtime_error(message);
    }
  }
  return std::make_shared<Schema>(Private(), std::move(names), std::move(types), std::move(index));
}

Schema::Schema(Private, std::vector<std::string> names, std::vector<ElementTypeId::Enum> types,
     std::map<std::string_view, size_t, std::less<>> index) : names_(std::move(names)), types_(std::move(types)),
     index_(std::move(index)) {}
Schema::~Schema() = default;

std::optional<size_t> Schema::GetColumnIndex(std::string_view name, bool strict) const {
  auto ip = index_.find(name);
  if (ip != index_.end()) {
    return ip->second;
  }
  // Not found: check strictness flag.
  if (!strict) {
    return {};
  }
  auto message = Stringf(R"(Column name "%o" not found)", name);
  throw std::runtime_error(DEEPHAVEN_DEBUG_MSG(message));
}
}  // namespace deephaven::dhcore::clienttable
