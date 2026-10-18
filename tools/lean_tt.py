"""lean_tt.py -- экономный разбор объекта Unity по его typetree.

UnityPy.read_typetree превращает vector<UInt8> в список Python-чисел: у
ULTRAKILL/Master compressedBlob на 464 МБ это ~3.7 ГБ памяти, ПК на 7.7 ГБ от
этого выключился. Здесь массивы примитивов отдаются компактно: байты -- как
memoryview на исходные данные, остальные числа -- как array.array.

    read(obj) -> (dict, spans)
spans: путь поля ("compressedBlob", "m_ParsedForm.m_Name", ...) -> (начало,
конец) в сырых байтах объекта; по ним делаются точечные правки без полной
пересериализации.
"""

import array
import struct

ALIGN = 0x4000

PRIM = {
    "SInt8": ("b", 1), "UInt8": ("B", 1), "char": ("B", 1), "bool": ("?", 1),
    "SInt16": ("h", 2), "UInt16": ("H", 2), "short": ("h", 2), "unsigned short": ("H", 2),
    "SInt32": ("i", 4), "UInt32": ("I", 4), "int": ("i", 4), "unsigned int": ("I", 4),
    "Type*": ("i", 4),
    "SInt64": ("q", 8), "UInt64": ("Q", 8), "long long": ("q", 8), "unsigned long long": ("Q", 8),
    "FileSize": ("Q", 8),
    "float": ("f", 4), "double": ("d", 8),
}
ARRAY_CODE = {"b": "b", "B": "B", "?": "B", "h": "h", "H": "H", "i": "i", "I": "I",
              "q": "q", "Q": "Q", "f": "f", "d": "d"}


class Reader:
    def __init__(self, data, build=True, max_depth_spans=99):
        self.d = memoryview(data)
        self.p = 0
        self.spans = {}
        self.build = build            # False: только продвигать позицию (оценка размеров)
        self.max_depth_spans = max_depth_spans

    def align(self):
        self.p = (self.p + 3) & ~3

    def read_node(self, node, path):
        start = self.p
        t = node.m_Type
        kids = node.m_Children
        if t in PRIM:
            fmt, sz = PRIM[t]
            v = struct.unpack_from("<" + fmt, self.d, self.p)[0]
            self.p += sz
        elif t == "string":
            n = struct.unpack_from("<i", self.d, self.p)[0]
            self.p += 4
            v = bytes(self.d[self.p:self.p + n]).decode("utf8", "replace")
            self.p += n
            self.align()  # строки всегда выравниваются
        elif t == "TypelessData":
            n = struct.unpack_from("<i", self.d, self.p)[0]
            self.p += 4
            v = self.d[self.p:self.p + n]
            self.p += n
        elif kids and kids[0].m_Type == "Array":
            v = self.read_array(kids[0], path)
        elif not self.build:
            v = None
            for c in kids:
                self.read_node(c, f"{path}.{c.m_Name}" if path else c.m_Name)
        elif t == "pair":
            v = (self.read_node(kids[0], path + ".first"), self.read_node(kids[1], path + ".second"))
        else:
            v = {}
            for c in kids:
                v[c.m_Name] = self.read_node(c, f"{path}.{c.m_Name}" if path else c.m_Name)
        if node.m_MetaFlag & ALIGN:
            self.align()
        if path.count(".") + path.count("[") < self.max_depth_spans:
            self.spans[path] = (start, self.p)
        return v

    def read_array(self, arr_node, path):
        n = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        elem = arr_node.m_Children[1]
        if elem.m_Type in PRIM and not elem.m_Children:
            fmt, sz = PRIM[elem.m_Type]
            raw = self.d[self.p:self.p + n * sz]
            self.p += n * sz
            if fmt in ("B", "b", "?"):
                v = raw                        # memoryview, без копии
            else:
                v = array.array(ARRAY_CODE[fmt])
                v.frombytes(raw)
        elif not self.build:
            v = None
            for i in range(n):
                self.read_node(elem, f"{path}[{i}]")
        else:
            v = [self.read_node(elem, f"{path}[{i}]") for i in range(n)]
        if arr_node.m_MetaFlag & ALIGN:
            self.align()
        return v


def read(obj, build=True, max_depth_spans=99):
    data = obj.get_raw_data()
    root = node_of(obj)
    r = Reader(data, build, max_depth_spans)
    v = {}
    for c in root.m_Children:
        v[c.m_Name] = r.read_node(c, c.m_Name)
    if root.m_MetaFlag & ALIGN:
        r.align()
    return v, r.spans, data


def node_of(obj):
    """typetree объекта: из файла, а в собранных плеерах (где его вырезают) --
    из встроенной базы UnityPy по версии Unity."""
    st = getattr(obj, "serialized_type", None)
    if st is not None and getattr(st, "node", None) is not None:
        return st.node
    return obj._get_typetree_node()


# ---------------------------------------------------------------- запись

class Writer:
    """Обратная операция к Reader: значение из read() -> байты по typetree."""

    def __init__(self):
        self.b = bytearray()

    def align(self):
        self.b += b"\0" * ((-len(self.b)) % 4)

    def write_node(self, node, v):
        t = node.m_Type
        kids = node.m_Children
        if t in PRIM:
            fmt, _ = PRIM[t]
            self.b += struct.pack("<" + fmt, v)
        elif t == "string":
            enc = v.encode("utf8") if isinstance(v, str) else bytes(v)
            self.b += struct.pack("<i", len(enc)) + enc
            self.align()
        elif t == "TypelessData":
            self.b += struct.pack("<i", len(v)) + bytes(v)
        elif kids and kids[0].m_Type == "Array":
            self.write_array(kids[0], v)
        elif t == "pair":
            self.write_node(kids[0], v[0])
            self.write_node(kids[1], v[1])
        else:
            for c in kids:
                self.write_node(c, v[c.m_Name])
        if node.m_MetaFlag & ALIGN:
            self.align()

    def write_array(self, arr_node, v):
        elem = arr_node.m_Children[1]
        n = len(v)
        self.b += struct.pack("<i", n)
        if elem.m_Type in PRIM and not elem.m_Children:
            fmt, _ = PRIM[elem.m_Type]
            if isinstance(v, (bytes, bytearray, memoryview)):
                self.b += bytes(v)
            elif isinstance(v, array.array):
                self.b += v.tobytes()
            else:
                self.b += struct.pack("<%d%s" % (n, fmt), *v)
        else:
            for x in v:
                self.write_node(elem, x)
        if arr_node.m_MetaFlag & ALIGN:
            self.align()


def write(obj, value):
    root = node_of(obj)
    w = Writer()
    for c in root.m_Children:
        w.write_node(c, value[c.m_Name])
    if root.m_MetaFlag & ALIGN:
        w.align()
    return bytes(w.b)
