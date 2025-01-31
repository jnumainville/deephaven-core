/*
 * Copyright (c) 2016-2022 Deephaven Data Labs and Patent Pending
 */
#include "tests/third_party/catch.hpp"
#include "tests/test_util.h"

using deephaven::client::TableHandle;
namespace deephaven::client::tests {
namespace {
void TestFilter(const char *description, const TableHandle &filtered_table,
    const std::vector<std::string> &ticker_data,
    const std::vector<double> &close_data);
}  // namespace

TEST_CASE("String Filter", "[strfilter]") {
  auto tm = TableMakerForTests::Create();
  auto table = tm.Table();

  auto import_date = table.GetStrCol("ImportDate");
  auto ticker = table.GetStrCol("Ticker");
  auto close = table.GetNumCol("Close");

  auto t2 = table.Where(import_date == "2017-11-01").Select(ticker, close);

  {
    std::vector<std::string> ticker_data = {"AAPL", "AAPL", "AAPL", "ZNGA", "ZNGA"};
    std::vector<double> close_data = {23.5, 24.2, 26.7, 538.2, 544.9};
    TestFilter("Contains A", t2.Where(ticker.contains("A")),
        ticker_data, close_data);
  }

  {
    std::vector<std::string> ticker_data = {};
    std::vector<double> close_data = {};
    TestFilter("Starts with BL", t2.Where(ticker.startsWith("BL")),
        ticker_data, close_data);
  }

  {
    std::vector<std::string> ticker_data = {"XRX", "XRX"};
    std::vector<double> close_data = {88.2, 53.8};
    TestFilter("Ends with X", t2.Where(ticker.endsWith("X")),
        ticker_data, close_data);
  }

  {
    std::vector<std::string> ticker_data = {"IBM"};
    std::vector<double> close_data = {38.7};
    TestFilter("Matches ^I.*M$", t2.Where(ticker.matches("^I.*M$")),
        ticker_data, close_data);
  }
}

namespace {
void TestFilter(const char *description, const TableHandle &filtered_table,
    const std::vector<std::string> &ticker_data,
    const std::vector<double> &close_data) {
  INFO(description);
  INFO(filtered_table.Stream(true));
  CompareTable(
      filtered_table,
      "Ticker", ticker_data,
      "Close", close_data
  );
}
}  // namespace
}  // namespace deephaven::client::tests {
