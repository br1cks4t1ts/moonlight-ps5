#!/usr/bin/env python3
"""
Builds a fake PS5 PKG for installation via etaHEN/Kstuff.
Uses moonlight_itemzbase.pkg as template and replaces content entries.
"""
import struct, sys, os, hashlib, argparse

WORKING_PKG = 'moonlight_itemzbase.pkg'

# ── SFO builder ────────────────────────────────────────────────────────────────

def make_sfo(title_id, title_name, content_id, app_ver="01.00"):
    params = sorted([
        ("APP_TYPE",    2, 4,    struct.pack("<I", 1)),
        ("APP_VER",     4, 8,    app_ver.encode().ljust(8,  b"\x00")),
        ("ATTRIBUTE",   2, 4,    struct.pack("<I", 0)),
        ("CATEGORY",    4, 4,    b"gd\x00\x00"),
        ("CONTENT_ID",  4, 48,   content_id.encode().ljust(48, b"\x00")),
        ("TITLE",       4, 128,  title_name.encode().ljust(128, b"\x00")),
        ("TITLE_ID",    4, 12,   title_id.encode().ljust(12, b"\x00")),
        ("VERSION",     4, 8,    app_ver.encode().ljust(8,  b"\x00")),
    ])
    n = len(params)
    idx_off = 20
    key_off = idx_off + n * 16

    key_tbl = b""
    key_offs = []
    for name, *_ in params:
        key_offs.append(len(key_tbl))
        key_tbl += name.encode() + b"\x00"
    while len(key_tbl) % 4:
        key_tbl += b"\x00"

    data_tbl_start = key_off + len(key_tbl)
    data_tbl = b""
    data_offs = []
    for _, fmt, max_len, val in params:
        while len(data_tbl) % 4:
            data_tbl += b"\x00"
        data_offs.append(len(data_tbl))
        data_tbl += val

    hdr = struct.pack("<4sIIII",
        b"\x00PSF", 0x101, key_off, data_tbl_start, n)
    idx = b""
    for i, (name, fmt, max_len, val) in enumerate(params):
        idx += struct.pack("<HHIII", key_offs[i], fmt, len(val), max_len, data_offs[i])
    return hdr + idx + key_tbl + data_tbl


def sha256(data):
    return hashlib.sha256(data).digest()


def align_up(x, a):
    return (x + a - 1) & ~(a - 1)


def make_pkg(fself_data, sfo_data, icon_data, title_id):
    """Build PKG using working PKG as template, replacing content entries only."""
    
    content_id = f"IV0000-{title_id}_00-MOONLIGHTSTREAMS"
    cid_bytes = content_id.encode().ljust(0x24, b"\x00")[:0x24]
    
    # Read the working PKG template
    with open(WORKING_PKG, 'rb') as f:
        template = f.read()
    
    # Parse header
    entry_count = struct.unpack(">I", template[0x10:0x14])[0]
    sc_entry_count = struct.unpack(">H", template[0x14:0x16])[0]
    table_offset = struct.unpack(">I", template[0x18:0x1C])[0]
    table_size = struct.unpack(">I", template[0x1C:0x20])[0]
    
    # Parse entry table from template
    entries = []
    for i in range(entry_count):
        offset = table_offset + i * 32
        ent = template[offset:offset+32]
        eid, name_off, fl1, fl2, data_off, data_sz = struct.unpack(">IIIIII", ent[:24])
        entries.append((eid, name_off, fl1, fl2, data_off, data_sz))
    
    # Content sizes
    sfo_sz = len(sfo_data)
    icon_sz = len(icon_data)
    fself_sz = len(fself_data)
    
    # Calculate new PKG size
    # Keep SC entries as-is (first 6 entries), recalculate content entry offsets
    HDR_SZ = 0x2000
    ENT_SZ = 32
    TBL_OFF = HDR_SZ
    TBL_SZ = entry_count * ENT_SZ
    
    # Calculate offsets for all entries, keeping SC entries in same positions
    # Entry 0-5 stay same, 6-8 stay same
    offsets = []
    cur = 0x2000 + TBL_SZ
    
    # Keep offsets for first 9 entries (0x0001 through 0x0409) from template
    sc_offsets = [0x2180, 0x2340, 0x2b40, 0x2c40, 0x2dc0, 0x2f80, 0x2fd0, 0x33d0, 0x35d0]
    
    # Content entries start after SC entries, aligned to 0x10
    cur = align_up(sc_offsets[-1] + entries[8][5], 0x10)  # After entry 8 (0x0409)
    
    new_offsets = list(sc_offsets)
    new_offsets.append(cur)  # Entry 9 (param.sfo)
    cur = align_up(cur + sfo_sz, 0x10)
    new_offsets.append(cur)  # Entry 10 (icon0.png)
    cur = align_up(cur + icon_sz, 0x10)
    new_offsets.append(cur)  # Entry 11 (eboot.bin)
    cur = align_up(cur + fself_sz, 0x10)
    
    total = cur
    
    # Build new header (copy template header, update content ID and sizes)
    hdr = bytearray(template[:HDR_SZ])
    hdr[0x40:0x64] = cid_bytes
    struct.pack_into(">Q", hdr, 0x28, total - 0x2000)  # body_size
    struct.pack_into(">Q", hdr, 0x38, total - 0x2000)  # content_size
    
    # Build entry table
    tbl = bytearray()
    for i, (eid, name_off, fl1, fl2, old_off, old_sz) in enumerate(entries):
        if i < 9:
            # SC entries - keep same offset but update size if needed
            data = template[new_offsets[i]:new_offsets[i]+old_sz]
            tbl += struct.pack(">IIIIII", eid, name_off, fl1, fl2, new_offsets[i], old_sz)
            tbl += b"\x00" * 8
        elif i == 9:
            # param.sfo
            tbl += struct.pack(">IIIIII", eid, name_off, fl1, fl2, new_offsets[i], sfo_sz)
            tbl += b"\x00" * 8
        elif i == 10:
            # icon0.png
            tbl += struct.pack(">IIIIII", eid, name_off, fl1, fl2, new_offsets[i], icon_sz)
            tbl += b"\x00" * 8
        else:
            # eboot.bin
            tbl += struct.pack(">IIIIII", eid, name_off, fl1, fl2, new_offsets[i], fself_sz)
            tbl += b"\x00" * 8
    
    # Build output
    out = bytearray(total)
    out[0:HDR_SZ] = bytes(hdr)
    out[TBL_OFF:TBL_OFF+len(tbl)] = tbl
    
    # Copy SC entry data from template
    for i in range(9):
        sz = entries[i][5]
        out[new_offsets[i]:new_offsets[i]+sz] = template[sc_offsets[i]:sc_offsets[i]+sz]
    
    # Insert our content
    out[new_offsets[9]:new_offsets[9]+sfo_sz] = sfo_data
    out[new_offsets[10]:new_offsets[10]+icon_sz] = icon_data
    out[new_offsets[11]:new_offsets[11]+fself_sz] = fself_data
    
    # Update digest table - hash all entries including new content
    # Entry digest: hash of each entry's data
    digest_entry_data = b''
    for i in range(12):
        off = new_offsets[i]
        sz = entries[i][5] if i < 9 else (sfo_sz if i == 9 else (icon_sz if i == 10 else fself_sz))
        digest_entry_data += sha256(bytes(out[off:off+sz]))
    
    # First digest is zero (no table to hash)
    digest_entry_data = b'\x00' * 32 + digest_entry_data
    
    # Place in entry 0
    out[new_offsets[0]:new_offsets[0]+len(digest_entry_data)] = digest_entry_data
    
    # Update SC entry hashes
    # SC entries are entries 0-5 (0x0001 through 0x0200)
    sc_data = b''
    for i in range(6):
        off = new_offsets[i]
        sz = entries[i][5]
        sc_data += bytes(out[off:off+sz])
    sc_entries1_hash = sha256(sc_data)
    
    # SC entries 2: entries 0-4 (excluding file names)
    sc_data2 = b''
    for i in range(5):
        off = new_offsets[i]
        sz = entries[i][5]
        sc_data2 += bytes(out[off:off+sz])
    sc_entries2_hash = sha256(sc_data2)
    
    # Body digest
    body_data = bytes(out[0x2000:total])
    body_digest = sha256(body_data)
    
    # Update header digests
    hdr[0x100:0x120] = sc_entries1_hash
    hdr[0x120:0x140] = sc_entries2_hash
    hdr[0x160:0x180] = body_digest
    out[0:HDR_SZ] = bytes(hdr)
    
    return bytes(out)


# ── Main ───────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fself", help="eboot.bin (FSELF)")
    ap.add_argument("output", help="output .pkg path")
    ap.add_argument("--title-id", default="MLPS00001")
    ap.add_argument("--title-name", default="Moonlight PS5")
    ap.add_argument("--icon", default=None)
    args = ap.parse_args()

    content_id = f"IV0000-{args.title_id}_00-MOONLIGHTSTREAMS"
    print(f"Title ID   : {args.title_id}")
    print(f"Content ID : {content_id}")

    with open(args.fself, "rb") as f:
        fself = f.read()
    print(f"FSELF size : {len(fself):,} bytes")

    sfo = make_sfo(args.title_id, args.title_name, content_id)
    print(f"param.sfo  : {len(sfo)} bytes")

    # Use custom icon if provided
    icon = None
    for icon_path in ["pkg-content/sce_sys/icon0.png", args.icon]:
        if icon_path and os.path.exists(icon_path):
            with open(icon_path, "rb") as f:
                icon = f.read()
            print(f"icon0.png  : {len(icon)} bytes ({icon_path})")
            break
    if icon is None:
        # Minimal 1x1 white PNG
        icon = bytes([0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,0xde,0x00,0x00,0x00,0x0c,0x49,0x44,0x41,0x54,0x08,0xd7,0x63,0xf8,0xcf,0xc0,0x00,0x00,0x00,0x02,0x00,0x01,0xe2,0x21,0xbc,0x33,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82])
        print(f"icon0.png  : {len(icon)} bytes (placeholder)")

    pkg = make_pkg(fself, sfo, icon, args.title_id)
    with open(args.output, "wb") as f:
        f.write(pkg)
    print(f"PKG written: {args.output}  ({len(pkg)/1024/1024:.1f} MB)")

if __name__ == "__main__":
    main()