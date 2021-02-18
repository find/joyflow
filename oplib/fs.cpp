#include <oplib.h>
#include <ophelper.h>

#include <filesystem>
#include <chrono>

using namespace joyflow;

class OpListDir : public OpKernel
{
public:
  void eval(OpContext& ctx) const override
  {
    namespace fs     = std::filesystem;
    auto*  odc       = ctx.reallocOutput(0);
    String dir       = ctx.arg("dir").asString();
    bool   recursive = ctx.arg("recursive").asBool();
    bool   stats     = ctx.arg("stats").asBool();

    odc->addTable();
    auto* odt        = odc->getTable(0);
    auto* namecolumn = odt->createColumn<String>("path");
    if (dir.empty())
      return;

    DataColumn *sizecolumn = nullptr, *permcolumn = nullptr, *typecolumn = nullptr;
    if (stats) {
      sizecolumn = odt->createColumn<size_t>("filesize", 0);
      permcolumn = odt->createColumn<uint32_t>("permissions", 0);
      typecolumn = odt->createColumn<String>("type");
    }
    static const std::unordered_map<fs::file_type, String> filetypemap = {
        {fs::file_type::none, "none"},
        {fs::file_type::not_found, "not_found"},
        {fs::file_type::regular, "regular"},
        {fs::file_type::directory, "directory"},
        {fs::file_type::symlink, "symlink"},
        {fs::file_type::block, "block"},
        {fs::file_type::character, "character"},
        {fs::file_type::fifo, "fifo"},
        {fs::file_type::socket, "socket"},
        {fs::file_type::unknown, "unknown"}};
    static const String unknown = "unknown";

    auto lsdir = [&](auto diriterator) {
      auto nameifc = namecolumn->asBlobData();
      for (auto entry : diriterator) {
        auto ci = odt->addRow();
        auto u8path = entry.path().u8string();
        nameifc->setBlob(ci, u8path.data(), u8path.size());
        if (stats) {
          auto status = entry.status();
          sizecolumn->set<size_t>(ci, entry.file_size());
          permcolumn->set<uint32_t>(ci, static_cast<uint32_t>(status.permissions()));
          typecolumn->set<String>(ci, lookup<String>(filetypemap, status.type(), unknown));
        }
      }
    };

    if (recursive) {
      lsdir(fs::recursive_directory_iterator(dir));
    } else {
      lsdir(fs::directory_iterator(dir));
    }
  }
};

OpDesc lsdirDesc()
{
  return makeOpDesc<OpListDir>("lsdir").numMaxInput(1).numRequiredInput(0).numOutputs(1).argDescs(
      {ArgDescBuilder("dir").label("Directory").type(ArgType::DIRPATH),
       ArgDescBuilder("recursive")
           .label("Recursive")
           .type(ArgType::TOGGLE)
           .defaultExpression(0, "false"),
       ArgDescBuilder("stats")
           .label("File Stats")
           .type(ArgType::TOGGLE)
           .defaultExpression(0, "false")});
}

class OpPathModTime : public OpKernel
{
  void eval(OpContext& ctx) const override
  {
    auto pathtabl = ctx.arg("table").asInt();
    auto pathattr = ctx.arg("filepath").asString();
    auto tmodattr = ctx.arg("outcol").asString();
    auto* odc = ctx.copyInputToOutput(0, 0);
    if (pathattr.empty() || tmodattr.empty()) { // do nothing
      return;
    }

    auto* odt = odc->getTable(pathtabl);
    RUNTIME_CHECK(odt, "Table {} does not exist", pathtabl);

    auto* pathcol = odt->getColumn(pathattr);
    RUNTIME_CHECK(pathcol, "Column \"{}\" cannot be found in table {}", pathattr, pathtabl);

    auto* tmodcol = odt->createColumn<String>(tmodattr);
    RUNTIME_CHECK(tmodcol, "Column \"{}\" cannot be created", tmodattr);

    auto fsnow = std::filesystem::file_time_type::clock::now();
    auto sysnow = std::chrono::system_clock::now();
    char buf[32] = { 0 };
    for (CellIndex ci{ 0 }; ci < odt->numIndices(); ++ci) {
      if (odt->getRow(ci) == -1)
        continue;

      auto t = std::filesystem::last_write_time(
        std::filesystem::u8path(
          pathcol->get<StringView>(ci)));
      auto systime = std::chrono::system_clock::to_time_t(t - fsnow + sysnow);
      auto len = std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&systime));
      tmodcol->set<StringView>(ci, StringView(buf, len));
    }
  }
};

OpDesc pathmodtimeDesc()
{
  return makeOpDesc<OpPathModTime>("file mod time").numMaxInput(1).numRequiredInput(1).numOutputs(1).argDescs(
  {
    op::tableSelectionArg("table", "Table", false),
    op::columnSelectionArg("table", "filepath", "File Path", "column that contains filepath").defaultExpression(0,"path"),
    ArgDescBuilder("outcol").label("Output Column").type(ArgType::STRING).defaultExpression(0, "mod_time")
  });
}

class OpPathExists : public OpKernel
{
public:
  void eval(OpContext &ctx) const override
  {
    auto pathtabl = ctx.arg("table").asInt();
    auto pathattr = ctx.arg("filepath").asString();
    auto existsattr = ctx.arg("exists").asString();
    auto* odc = ctx.copyInputToOutput(0, 0);
    if (pathattr.empty() || existsattr.empty()) { // do nothing
      return;
    }

    auto* odt = odc->getTable(pathtabl);
    RUNTIME_CHECK(odt, "Table {} does not exist", pathtabl);

    auto* pathcol = odt->getColumn(pathattr);
    RUNTIME_CHECK(pathcol, "Column \"{}\" cannot be found in table {}", pathattr, pathtabl);

    auto* existscol = odt->createColumn<bool>("exists", true);
    RUNTIME_CHECK(existscol, "column \"exists\" cannot be created");
    existscol->asFixSizedData()->setToStringMethod([](const void* b)->String {return *(static_cast<bool const*>(b)) ? "true" : "false"; });

    for (CellIndex ci{ 0 }; ci < odt->numIndices(); ++ci) {
      if (odt->getRow(ci) == -1)
        continue;
      auto path = std::filesystem::u8path(pathcol->get<StringView>(ci));
      existscol->set<bool>(ci, std::filesystem::exists(path));
    }
  }
};

OpDesc pathexistsDesc()
{
  return makeOpDesc<OpPathExists>("path exists").numMaxInput(1).numRequiredInput(1).numOutputs(1).argDescs(
  {
    op::tableSelectionArg("table", "Table", false),
    op::columnSelectionArg("table", "filepath", "File Path", "column that contains filepath").defaultExpression(0,"path"),
    ArgDescBuilder("outcol").label("Output Column").type(ArgType::STRING).defaultExpression(0, "exists")
  });
}

