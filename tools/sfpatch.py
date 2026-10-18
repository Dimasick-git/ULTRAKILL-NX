"""sfpatch.py -- точечная замена объектов в сериализованном файле Unity (формат 22+).

UnityPy при сохранении .assets собранного плеера ломает служебную часть
(«sharedassets0.assets is corrupted!»). Здесь заголовок и метаданные остаются
байт в байт; меняются только данные заменяемых объектов, их byteStart/byteSize
в таблице объектов и размер файла в заголовке.

    replace_objects(data: bytes, {path_id: new_bytes}) -> bytes
"""

import struct


def _cstr(d, p):
    e = d.index(b"\0", p)
    return d[p:e].decode("utf8", "replace"), e + 1


def parse(d):
    msize, fsize32, version, doff32 = struct.unpack_from(">IIII", d, 0)
    if version < 22:
        raise ValueError(f"формат {version} не поддерживается (нужен 22+)")
    endian = d[16]
    msize, fsize, doff = struct.unpack_from(">IQQ", d, 20)
    e = "<" if endian == 0 else ">"
    p = 48
    unity_version, p = _cstr(d, p)
    platform = struct.unpack_from(e + "i", d, p)[0]
    p += 4
    typetree = d[p] != 0
    p += 1
    if typetree:
        raise ValueError("файл с typetree -- этот патчер только для файлов плеера без него")
    ntypes = struct.unpack_from(e + "i", d, p)[0]
    p += 4
    for _ in range(ntypes):
        class_id = struct.unpack_from(e + "i", d, p)[0]
        p += 4
        p += 1                                  # IsStrippedType
        script_idx = struct.unpack_from(e + "h", d, p)[0]
        p += 2
        if class_id == 114 or script_idx >= 0:  # MonoBehaviour: ScriptID
            p += 16
        p += 16                                 # OldTypeHash
    nobj = struct.unpack_from(e + "i", d, p)[0]
    p += 4
    objs = []
    for _ in range(nobj):
        p = (p + 3) & ~3
        pid, start, size, tid = struct.unpack_from(e + "qQIi", d, p)
        objs.append({"pid": pid, "start": start, "size": size, "type": tid, "entry": p})
        p += 24
    return {"endian": e, "msize": msize, "fsize": fsize, "doff": doff, "objs": objs,
            "unity": unity_version, "platform": platform}


def replace_objects(d, new, page=4096):
    """Объекты до первого заменённого остаются на своих местах; каждый
    следующий сдвигается на накопленный прирост, округлённый до page байт, --
    так у всех объектов сохраняется исходное выравнивание."""
    info = parse(d)
    e, doff = info["endian"], info["doff"]
    objs = sorted(info["objs"], key=lambda o: o["start"])
    head = bytearray(d[:doff])
    body = bytearray()
    shift = 0
    for i, o in enumerate(objs):
        start = o["start"] + shift
        if len(body) < start:
            body += b"\0" * (start - len(body))
        data = new.get(o["pid"])
        if data is None:
            data = d[doff + o["start"]: doff + o["start"] + o["size"]]
        struct.pack_into(e + "QI", head, o["entry"] + 8, start, len(data))
        body[start:start] = b""            # позиция уже дополнена нулями
        body += data
        if o["pid"] in new:
            nxt = objs[i + 1]["start"] if i + 1 < len(objs) else o["start"] + o["size"]
            room = nxt - o["start"]          # место до следующего объекта в оригинале
            grow = len(data) - room
            if grow > 0:
                shift += (grow + page - 1) // page * page
    tail_end = max((o["start"] + o["size"] for o in objs), default=0)
    orig_len = len(d) - doff
    if orig_len > tail_end:                  # хвост после последнего объекта
        body += d[doff + tail_end:]
    out = head + body
    struct.pack_into(">Q", out, 24, len(out))
    return bytes(out)
