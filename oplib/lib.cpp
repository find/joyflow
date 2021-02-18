#include <oplib.h>

#include <string>
#include <set>

DECL_OPLIB();
IMPL_OPLIB();

static std::set<std::string>& myLibs()
{
  static std::set<std::string> libs;
  return libs;
}

joyflow::OpDesc csvReaderDesc();
joyflow::OpDesc csvWriterDesc();
joyflow::OpDesc lsdirDesc();
joyflow::OpDesc pathmodtimeDesc();
joyflow::OpDesc pathexistsDesc();

void regOneOp(joyflow::OpDesc const& desc)
{
  joyflow::OpRegistry::instance().add(desc);
  myLibs().insert(desc.name);
}

OPLIB_API void openLib()
{
  regOneOp(csvReaderDesc());
  regOneOp(csvWriterDesc());
  regOneOp(lsdirDesc());
  regOneOp(pathmodtimeDesc());
  regOneOp(pathexistsDesc());
}

OPLIB_API void closeLib()
{
  for (auto const& name: myLibs()) {
    joyflow::OpRegistry::instance().remove(name);
  }
}

