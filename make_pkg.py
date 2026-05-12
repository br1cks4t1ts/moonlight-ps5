#!/usr/bin/env python3
"""
Builds a fake PS5 PKG for installation via etaHEN DPI v2.
Entry structure reverse-engineered from ItemzFlow PS5_ITEM00001_v1.14.pkg.
"""
import struct, sys, os, hashlib, argparse

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


# ── Minimal 1x1 white PNG ──────────────────────────────────────────────────────
TINY_PNG = bytes([
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,
    0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
    0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
    0xde,0x00,0x00,0x00,0x0c,0x49,0x44,0x41,
    0x54,0x08,0xd7,0x63,0xf8,0xcf,0xc0,0x00,
    0x00,0x00,0x02,0x00,0x01,0xe2,0x21,0xbc,
    0x33,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,
    0x44,0xae,0x42,0x60,0x82,
])


# ── NP DRM / crypto entries from ItemzFlow PS5_ITEM00001_v1.14.pkg ─────────────
# These are extracted verbatim; etaHEN bypasses signature checks but the installer
# still requires these entries to be present in the table for container lookup.

ENTRY_0020 = bytes.fromhex('ca16c5a4b80f54ff4dd2939a0afed6450b60621c21d3203792a467d92ca77b056f8fe166357fbae47b85741c20a79ebaa1b850d3f1172bdba0af8f8c6cfc5dab5c9da301e125ebc77c9a9ccd677ba15c4255259a6aaf7910304f1fdeccf6fbbe870e1572a3d752b149baf35e365c55c897d61224e87120748174b305a9f8600e23de19ba2cd27dd50efcec9143acc9c22acac539b76122dbdd086ff89ff4dee3a018fdc87a3cc415014a61b5620e5c7cdb3b44987dcafbf99776eb5e09a3941797a41e9f4becae4ff899ba6d4954eb61eb7998b50366ad699e8cdf9c4435a534edf1ec99f41aeffc9d637d2081bc37a92080a51c7d4b38f24523ff5887496597')

# Entry 0x0100 from working PKG = the working PKG's own entry table (used for NP DRM container lookup)
ENTRY_0100 = bytes.fromhex('0000000100000000400000000000000000002c40000001c0000000000000000000000010000000006000000000000000000020000000080000000000000000000000002000000000e0000000000030000000280000000100000000000000000000000080000000006000000000000000000029000000018000000000000000000000010000000000600000000000000000002a80000001c000000000000000000000020000000000400000000000000000002e000000004b0000000000000000000004000000000080000000000030000000406000000400000000000000000000000401000000008000000000002000000044600000020000000000000000000000040900000000000000000000000000004db0000020000000000000000000000010000000000b0000000000000000000046600000074c00000000000000000000100100000015000000000000000000002e50000001a000000000000000000000100200000026000000000000000000002ff000000efc00000000000000000000100300000037000000000000000000003ef00000017000000000000000000000120000000001000000000000000000006db00002d8cd0000000000000000')

ENTRY_0400 = bytes.fromhex('11c747f1e27a52f9cf07ed47f75d1a4d1599b120523275be4b8fdb459ce9f84c3b772012dbe58cb090c6f989f9b518aae1174f853155020d359a02d6fd63c80cbe534f0f3ee54f7b9e9ef64e77e302b028e8be2b51a575ef890569edaa64b8d279bcfc8cc03cce56c1409f02ded63fc107d2ab7be3788e76a83d77fa8486d26e874244ca808509b10d20d94fbfa4a5255cf1621fe162957405bfffcc27ef635db0276a7aaca3faae7674826e58851bd71b021dc0ac6c6605a7530170f4b911d39a2e9287b6958b9c6bf1ac7caca07372619d56fdde068edf1cbe928557e5ca83b279112d3ebb8566893ed25d25ab1b2acee6b1d01e7e1554d918d1a9dd8e1eb658bc66f918e7b9d5246839b14bd5dcb74a420553a4cfb27149c6965c245095188ac7ccfff0c9cb57b736c0421de81e4b542ebd3c95b3377f472c340326964f1511e7936cc517d60b206ac240248ca40f6fb35c6ab283f18663becb1ca11144308561e3edd20caa4d1a3f0f80af4e3603084c4b42f325c4bb409bdd647a2b5e9e34af14efb382119ffbc0d4870a17844770c13abf98fe8e4cca68e1280b1185dc27b3aa2c2b79f1af1a5efcf6d540b02db6ad6f4c67c3e4d1b87706f62b02165964a81f5475ce727c189e2ce5923b5d89f0cfe102a839882a96e2d0c7f0e3946fb11b881bb5a68064235a81765d191d84acdc1e669fd88ce551babbf3de0ed10c9a22955268c360a7a02a96c41a8dd57469da9cb41a739776f161f9173d87d52adbf287327c0a4f114404fa2c69c689df597fdac446b9bcef4f77d211c2bd341dffd71bdaee5e619bf69945bae2f3f92b6b98366c5350308d0e5b79fc1d00b9bdb3d43904babc26508f3909cf5cd170f76d4caa98f875d7fb8aee83209bf0d92a076c472dc8b61931a1b598415643dfb4b253a2368877df38682400ef0dae1d10ea595bf1663ce3daa84379504735e716527d0c8ffa8b0a5a0f361bdf17204ca7807e22623c6cb0d87d2b45901379843e1d70cffc1ac7994431b08a01ed1cfaf31298e8e7b6b4dcadc12cd10e092494e8063aaf933e06ff260649c94106e8e9dc57deda4c86e4bff9096aa7c8ad3e3d66ad1f288119f58954cc8b6187a70743eb4e27663902f24fddfb5dd9600f706de8219558671ed1f4c05ee2ff8b42a3fb596c62306c61dca46fb6dd545a7574f69ff4513acb9c36c39c1d356da2d2459097e43a060bc04d6756fd04ed3f18622bc1b4a0cd46ed3d23e8ef4699a8c5f510c26d4acbead1ece03883a3c9af9539fdffdcd345409a2ae5bfa87e7f91ee5bc4f8e01ebd373480d62486a5492f26d61477d2f9d28431e0c24dc27c50f4089e3a726f76f03b04c71ce00145b4603d2a19f8fe0d4e68c4a6883336f766ad0025d200cc119d7de6df1f4fba23c300e35ed2a6bd7b63b739d8ecbd1ab62060c674692e')

ENTRY_0401 = bytes.fromhex('a321eafd82d62ed41251be5204e8f35596c853d95664c095585a7ca0e9e25ca129da73d1286eeb42b30cb4796a4c5ad3c50bc75f491c723545ad05a484a42f845522cb68660c9c22b9f7472620310578035a198aed3fe210143de9ad52bda948751329c6ea3d7af48d2990f9cf2d192f04d9f94f67f1b3a5ff389aa18b37004fde354850886bac64a019da46fd6e0491de0c2f8eb9684aa14bd6aa5a77fdb6f029e6775cbedbe214fcb8fcd8cbee25ae88d69116b9640d41c87c1a23b053636ccd102b77b8286506b17c205dab01df56d0c5ac0938a64cb2e516af1a37045bc36a01d632124a89a747cfecd38f202d56bb5bfc2f4ef3a77072a08890ba6a29af3eef5b36fb7ccc51a91865dc2bc52be790476faf64507feb279f2a050d2bc221b48a4dc546fa52fc69725bfca1b10b172e8b2c3fdb4beba638eda88d4386c6ba77f9f73e725b20902f1c3f7a78eedec31afca2ce5c1b8e114b794811b18345c3022697ca36dee03b362b5633628be17f499502db027b137ebe3759b295c7e666dd664d341cb3a1ce79b9f087da053e413e55547fd1c2c2806b9e0dfbdfc1573cbf884522242077cd5e4061435696ccd6c3dd0c5caec2519e09e55861fdc59a63890f90cdb69b0cbb13f0173698d5ca61b89ca67ae2f920727335695c9b1c836804252068e87f0d5547e0779458708f85546b95da3b419fe41586a28a92e3f627')

ENTRY_0409 = b'\x00' * 0x2000

# Subcontainer blob (entry 0x0080) — from working PKG
SUBCONTAINER = (
    b'\xd2\x56\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
    b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x6e\x7c'
    b'\x84\x12\xf2\x64\x4f\x01\xdd\x47\x8b\xad\x19\xe8\x7e\x5a\x2c\x99'
    b'\xd9\xac\x94\x93\xfa\xfb\x52\x26\xd8\x8e\x2d\x89\xbd\x93\xff\x67'
    b'\xc6\xf7\xc5\x72\xed\xde\x90\xe8\x0d\xb6\x1a\x97\xe7\x92\xe7\x49'
    b'\x98\xd2\x2f\x60\x39\xc0\xed\x70\xf1\x27\x5b\x5a\x4f\x86\x6c\x98'
    b'\x0d\x32\x79\x2a\xe4\x04\xa0\x5c\xb7\xf2\x34\x1f\xe0\xec\xa8\x8f'
    b'\xce\x61\xcb\xcc\xc0\x59\x54\x96\x5f\xbb\xe8\x4c\x09\x2f\xc4'
) + b'\x00' * (0x180 - 0x7f)


# ── PKG builder ────────────────────────────────────────────────────────────────

def align_up(x, a):
    return (x + a - 1) & ~(a - 1)

def sha256(data):
    return hashlib.sha256(data).digest()


def make_pkg(fself_data, sfo_data, icon_data, title_id):
    content_id = f"IV0000-{title_id}_00-MOONLIGHTSTREAMS"
    cid_bytes  = content_id.encode().ljust(0x24, b"\x00")[:0x24]

    # File name table: leading null + null-terminated filenames (matches working PKG convention)
    # Offsets: icon0.png=1, param.sfo=11, eboot.bin=21
    file_name_table = b'\x00icon0.png\x00param.sfo\x00eboot.bin\x00'

    # Build entry table — 12 entries matching real PKG structure
    # Digest entry size = 32 bytes × n_ents; computed after n_ents is known
    # (placeholder replaced below)
    all_entries = [
        (0x0001, 0, 0x40000000, 0x0000, b''),             # digests — placeholder
        (0x0010, 0, 0x60000000, 0x0000, file_name_table), # entry name table
        (0x0020, 0, 0xe0000000, 0x3000, ENTRY_0020),      # crypto hash material
        (0x0080, 0, 0x60000000, 0x0000, SUBCONTAINER),    # subcontainer
        (0x0100, 0, 0x60000000, 0x0000, ENTRY_0100),      # entry table ref (working PKG copy)
        (0x0200, 0, 0x40000000, 0x0000, file_name_table), # file name table
        (0x0400, 0, 0x80000000, 0x3000, b'\x00' * len(ENTRY_0400)),  # NP DRM header (zeroed)
        (0x0401, 0, 0x80000000, 0x2000, b'\x00' * len(ENTRY_0401)),  # NP DRM cert (zeroed)
        (0x0409, 0, 0x00000000, 0x0000, ENTRY_0409),      # reserved (zeroed)
        (0x1000,  1, 0x00000000, 0x0000, sfo_data),       # param.sfo  (name_off=11 param.sfo)
        (0x1200,  1, 0x00000000, 0x0000, icon_data),      # icon0.png  (name_off=1)
        (0x1400, 21, 0x00000000, 0x0000, fself_data),     # eboot.bin  (name_off=21)
    ]
    n_ents = len(all_entries)

    # Fix digest size and param.sfo name offset now that n_ents is known
    digest_data = b'\x00' * (32 * n_ents)
    all_entries[0]  = (0x0001, 0,  0x40000000, 0x0000, digest_data)
    all_entries[9]  = (0x1000, 11, 0x00000000, 0x0000, sfo_data)   # name_off=11 for param.sfo

    # sc_entry_count = entries whose id < 0x0400 (metadata / SC entries) = 6
    sc_entry_count = sum(1 for eid, *_ in all_entries if eid < 0x0400)

    HDR_SZ  = 0x2000
    ENT_SZ  = 32
    TBL_OFF = HDR_SZ
    TBL_SZ  = n_ents * ENT_SZ
    DAT_OFF = align_up(TBL_OFF + TBL_SZ, 0x10)

    offsets = []
    cur = DAT_OFF
    for *_, data in all_entries:
        offsets.append(cur)
        cur = align_up(cur + len(data), 0x10)
    total = cur

    # NP DRM content header at 0x400 (version field = 1 required by sceNpDrmCheckContentHeader)
    CONTENT_HDR = (
        b"\x00\x00\x00\x01\x00\x00\x00\x01\x80\x00\x00\x00\x00\x00\x03\xcc"
        b"\x00\x00\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\x03\xb7\x00\x00"
        b"\x00\x00\x00\x00\x03\xbf\x00\x00\x00\x01\x00\x00\x00\x0d\x00\x00"
        b"\x67\xc6\xf7\xc5\x72\xed\xde\x90\xe8\x0d\xb6\x1a\x97\xe7\x92\xe7"
        b"\x49\x98\xd2\x2f\x60\x39\xc0\xed\x70\xf1\x27\x5b\x5a\x4f\x86\x6c"
        b"\xba\xf5\xfa\xf5\x21\x1b\x31\x95\xc5\x9d\x3b\x03\xef\xbc\x7c\x52"
        b"\x72\x57\x29\x44\x31\x8c\x3e\x15\x16\x65\xab\x31\x41\x5c\x94\x45"
        + b"\x00" * (0x3cc - 0x70)
    )

    hdr = bytearray(HDR_SZ)
    struct.pack_into(">I",  hdr, 0x00, 0x7F434E54)        # magic
    struct.pack_into(">H",  hdr, 0x04, 0x0000)             # revision
    struct.pack_into(">H",  hdr, 0x06, 0x0001)             # type
    struct.pack_into(">I",  hdr, 0x0C, n_ents)             # file_count (match n_ents like working PKG)
    struct.pack_into(">I",  hdr, 0x10, n_ents)             # entry_count
    struct.pack_into(">H",  hdr, 0x14, sc_entry_count)     # sc_entry_count = 6
    struct.pack_into(">H",  hdr, 0x16, n_ents)             # entry_count_2
    struct.pack_into(">I",  hdr, 0x18, TBL_OFF)            # table_offset
    struct.pack_into(">I",  hdr, 0x1C, TBL_SZ)            # sc_entry_data_size
    struct.pack_into(">Q",  hdr, 0x20, DAT_OFF)            # body_offset
    struct.pack_into(">Q",  hdr, 0x28, total - DAT_OFF)    # body_size
    struct.pack_into(">Q",  hdr, 0x30, DAT_OFF)            # content_offset
    struct.pack_into(">Q",  hdr, 0x38, total - DAT_OFF)    # content_size
    hdr[0x40:0x40+0x24] = cid_bytes
    struct.pack_into(">I",  hdr, 0x70, 0x0000000F)         # drm_type = fake
    struct.pack_into(">I",  hdr, 0x74, 0x0000001A)         # content_type = PS5 app
    struct.pack_into(">I",  hdr, 0x78, 0x0A000000)         # content_flags
    struct.pack_into(">I",  hdr, 0x98, 0x00000000)         # iro_tag
    hdr[0x400:0x400+len(CONTENT_HDR)] = CONTENT_HDR

    EXTENDED_HDR = bytes.fromhex('b85d1d452f9fef4d25d4cd7d71e1b7ae9071dee24fa2c8b1d0e231cf2183638300b148d49f3e3a0cdf79c144d03e60d2a84d73a82588bcc207b32e6cf1ad1a7bb2188fb051a8f732073a05a206731e90059daf29e669b96b5e186a689fcba880c91d703bafcddf1d1799049255211951a6cb1d474141491c5dc87a4b96f63628c05ce0e3ac347c0c3e9e42c3067bec29c32f2a1342ea0d9e297c57738e56645ccf2e252fccc42f18a31c06de0f8792a804808184bcc3f0cf3749c04d04b0c18c480a4f22ae1e62804e5bd0cece025b55ae1d37a61a1a72c5e0b816f268281b3d7db8106cbdb1097fb29d8d26dca5ec33b7ec929cc4d7d6938a759546d34f0ad7')
    hdr[0x1000:0x1000+len(EXTENDED_HDR)] = EXTENDED_HDR

    tbl = bytearray()
    for i, (eid, name_off, fl1, fl2, data) in enumerate(all_entries):
        tbl += struct.pack(">IIIIII",
            eid, name_off, fl1, fl2, offsets[i], len(data))
        tbl += b"\x00" * 8

    # Build output first (needed for body digest computation)
    out = bytearray(total)
    out[0:HDR_SZ] = hdr
    out[TBL_OFF:TBL_OFF+len(tbl)] = tbl
    for i, (*_, data) in enumerate(all_entries):
        out[offsets[i]:offsets[i]+len(data)] = data

    # Compute digests in correct order:
    # 1. Body digest = SHA256 of body content
    body_data = bytes(out[DAT_OFF:total])
    body_digest = sha256(body_data)

    # 2. SC entry hashes - hash the DATA of SC entries (entries with ID < 0x0400)
    sc_entries = []
    for eid, name_off, fl1, fl2, data in all_entries:
        if eid < 0x0400:
            sc_entries.append((eid, data))
    sc_entries.sort(key=lambda x: x[0])
    
    sc_data_1 = b''
    for eid, data in sc_entries[:5]:
        sc_data_1 += data
    sc_entries1_hash = sha256(sc_data_1)
    
    sc_data_2 = b''
    for eid, data in sc_entries[:4]:
        sc_data_2 += data
    sc_entries2_hash = sha256(sc_data_2)

    # 3. Digest table hash = SHA256(sc_entries1_hash + sc_entries2_hash + body_digest)
    # (This hashes the first 3 digests, NOT including itself to avoid circular ref)
    digest_table_hash = sha256(sc_entries1_hash + sc_entries2_hash + body_digest)

    # Place all digests in header
    hdr[0x100:0x120] = sc_entries1_hash
    hdr[0x120:0x140] = sc_entries2_hash
    hdr[0x140:0x160] = digest_table_hash
    hdr[0x160:0x180] = body_digest
    out[0:HDR_SZ] = hdr  # Update header with computed digests

    return bytes(out)


# ── Main ───────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fself",        help="eboot.bin (FSELF)")
    ap.add_argument("output",       help="output .pkg path")
    ap.add_argument("--title-id",   default="MLPS00001")
    ap.add_argument("--title-name", default="Moonlight PS5")
    ap.add_argument("--icon",       default=None)
    args = ap.parse_args()

    content_id = f"IV0000-{args.title_id}_00-MOONLIGHTSTREAMS"
    print(f"Title ID   : {args.title_id}")
    print(f"Content ID : {content_id}")

    with open(args.fself, "rb") as f:
        fself = f.read()
    print(f"FSELF size : {len(fself):,} bytes")

    sfo = make_sfo(args.title_id, args.title_name, content_id)
    print(f"param.sfo  : {len(sfo)} bytes")

    # Try custom icon from pkg-content/sce_sys/icon0.png, then args.icon, then fallback to placeholder
    icon = TINY_PNG
    icon_source = "placeholder"
    for icon_path in ["pkg-content/sce_sys/icon0.png", args.icon]:
        if icon_path and os.path.exists(icon_path):
            with open(icon_path, "rb") as f:
                icon = f.read()
            icon_source = icon_path
            break
    print(f"icon0.png  : {len(icon):,} bytes ({icon_source})")

    pkg = make_pkg(fself, sfo, icon, args.title_id)
    with open(args.output, "wb") as f:
        f.write(pkg)
    print(f"PKG written: {args.output}  ({len(pkg)/1024/1024:.1f} MB)")

if __name__ == "__main__":
    main()
