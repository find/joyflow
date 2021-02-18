import gdb
import re

def register_printer(typename):
    def _wrap(printer):
        def _printfunction(val):
            if re.match(typename, str(val.type)):
                return printer(val)
        gdb.pretty_printers.append(_printfunction)
    return _wrap

@register_printer('^(const )?joyflow::Vector<.*>$')
class VectorPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        if self.val['ptr_'] == 0:
            return '<empty>'
        content = ', '.join([str(self.val['ptr_'][i]) for i in range(self.val['size_'])])
        return '<Vector<{}>(size: {}, cap: {})['.format(self.val['ptr_'][0].type, self.val['size_'], self.val['capacity_'])+content+']'

@register_printer('^(const )?IntrusivePtr<.*>$')
class PointerPrinter:
    _unboxtypes = ('char','unsigned char', 'short', 'unsigned short', 'int', 'unsigned int', '__int64', 'unsigned __int64', 'float', 'double')

    def __init__(self, val):
        self.val = val

    def to_string(self):
        if self.val['ptr_'] == 0:
            return 'nullptr'
        vtype = self.val['ptr_'][0].type
        content = '{}(*{})'.format(self.val['ptr_'], self.val['ptr_'][0]['refcnt_']['_M_i'])
        if vtype in self._unboxtypes:
            content += ' -> {}'.format(self.val['ptr_'][0])
        return '<{}>{{{}}}'.format(vtype, content)

@register_printer('^(const )?joyflow::CellIndex$')
class CellIndexPrinter:
    def __init__(self, val):
        self.val = val
    def to_string(self):
        return '{}'.format(self.val['index_'])
