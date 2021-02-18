#include <oplib.h>
#include <csv.hpp>
#ifdef ERROR
#undef ERROR
#endif

using namespace joyflow;

class OpCSVReader : public OpKernel
{
public:
  void eval(OpContext& ctx) const override
  {
    auto filename = ctx.arg("file").asString();
    auto csvrdr = csv::CSVReader(filename);
    auto cols   = csvrdr.get_col_names();
    // auto nrows = csvrdr.n_rows();
    auto odc = ctx.reallocOutput(0);
    odc->addTable();
    auto odt = odc->getTable(0);
    for (auto const& col : cols) {
      odt->createColumn<String>(col);
    }
    // auto c = odt->addRows(nrows);
    for (auto const& row : csvrdr) {
      auto c = odt->addRow();
      for (auto const& col : cols) {
        auto sv = row[col].get<csv::string_view>();
        odt->set<StringView>(col, c, StringView(sv.data(), sv.size()));
      }
    }
  }
};

OpDesc csvReaderDesc()
{
  return makeOpDesc<OpCSVReader>("csv_reader")
    .icon(/*ICON_FA_FILE_CSV*/"\xEF\x9B\x9D" /*ICON_FA_ARROW_RIGHT*/"\xEF\x81\xA1")
    .numMaxInput(1)
    .numRequiredInput(0)
    .numOutputs(1)
    .argDescs({ArgDescBuilder("file")
                   .label("CSV File")
                   .type(ArgType::FILEPATH_OPEN)
                   .defaultExpression(0, "example.csv")
                   .fileFilter("csv")});
}

class OpCSVWriter : public OpKernel
{
public:
  void eval(OpContext& ctx) const override
  {
    auto filename = ctx.arg("file").asString();
    auto tid = ctx.arg("table").asInt();
    auto outstream = std::ofstream(filename);
    RUNTIME_CHECK(outstream, "cannot write to {}", filename);
    auto csvwtr = csv::CSVWriter<std::ofstream>(outstream);
    
    auto* odc = ctx.copyInputToOutput(0, 0);
    auto* dt = odc->getTable(tid);
    RUNTIME_CHECK(dt, "table {} does not exist", tid);

    auto colnames = dt->columnNames();
    sint const numcols = colnames.ssize();
    csvwtr << colnames;
    std::vector<String> strrow(numcols);
    std::vector<DataColumn*> columns(numcols);
    for (sint c = 0; c < numcols; ++c) {
      columns[c] = dt->getColumn(colnames[c]);
      DEBUG_ASSERT(columns[c] != nullptr);
    }
    for (sint row = 0, numrows = dt->numRows(); row < numrows; ++row) {
      auto ci = dt->getIndex(row);
      for (sint c = 0; c<numcols; ++c) {
        strrow[c] = columns[c]->toString(ci);
      }
      csvwtr << strrow;
    }
  }
};

OpDesc csvWriterDesc()
{
  return makeOpDesc<OpCSVWriter>("csv_writer")
    .icon(/*ICON_FA_ARROW_RIGHT*/"\xEF\x81\xA1" /*ICON_FA_FILE_CSV*/"\xEF\x9B\x9D")
    .numMaxInput(1)
    .numRequiredInput(1)
    .numOutputs(1)
    .argDescs({
      op::tableSelectionArg("table", "Table", false),
      ArgDescBuilder("file")
        .label("CSV File")
        .type(ArgType::FILEPATH_SAVE)
        .defaultExpression(0, "example.csv")
        .fileFilter("csv")});
}
