#!/usr/bin/env python3
"""
Disassembler and inspector for classes.dex from Heroes Lore 5 APK.
Extracts all classes, fields, methods, strings, and Dalvik instructions
into reference/dex_dump.txt so we have the exact Java <-> JNI contract.
"""
import struct
import os

with open('reference/extracted_apk/classes.dex', 'rb') as f:
    dex = f.read()

(magic, checksum, sig, file_size, header_size, endian_tag,
 link_size, link_off, map_off,
 string_ids_size, string_ids_off,
 type_ids_size, type_ids_off,
 proto_ids_size, proto_ids_off,
 field_ids_size, field_ids_off,
 method_ids_size, method_ids_off,
 class_defs_size, class_defs_off,
 data_size, data_off) = struct.unpack('<8sI20sIIIIIIIIIIIIIIIIIIII', dex[:112])

def read_uleb128(data, off):
    result = 0
    shift = 0
    while True:
        b = data[off]
        off += 1
        result |= (b & 0x7f) << shift
        if (b & 0x80) == 0:
            break
        shift += 7
    return result, off

strings = []
for i in range(string_ids_size):
    str_off, = struct.unpack('<I', dex[string_ids_off + i*4 : string_ids_off + (i+1)*4])
    utf16_len, pos = read_uleb128(dex, str_off)
    end = dex.find(b'\x00', pos)
    strings.append(dex[pos:end].decode('utf-8', errors='replace'))

types = []
for i in range(type_ids_size):
    idx, = struct.unpack('<I', dex[type_ids_off + i*4 : type_ids_off + (i+1)*4])
    types.append(strings[idx])

protos = []
for i in range(proto_ids_size):
    shorty_idx, ret_idx, params_off = struct.unpack('<III', dex[proto_ids_off + i*12 : proto_ids_off + (i+1)*12])
    ret_type = types[ret_idx]
    params = []
    if params_off != 0:
        sz, = struct.unpack('<I', dex[params_off:params_off+4])
        for j in range(sz):
            t_idx, = struct.unpack('<H', dex[params_off+4+j*2 : params_off+6+j*2])
            params.append(types[t_idx])
    protos.append((ret_type, params))

fields = []
for i in range(field_ids_size):
    c_idx, t_idx, n_idx = struct.unpack('<HHI', dex[field_ids_off + i*8 : field_ids_off + (i+1)*8])
    fields.append((types[c_idx], types[t_idx], strings[n_idx]))

methods = []
for i in range(method_ids_size):
    c_idx, p_idx, n_idx = struct.unpack('<HHI', dex[method_ids_off + i*8 : method_ids_off + (i+1)*8])
    ret_type, params = protos[p_idx]
    methods.append((types[c_idx], strings[n_idx], ret_type, params))

def disassemble_code(code_off):
    registers_size, ins_size, outs_size, tries_size, debug_off, insns_size = struct.unpack('<HHHHII', dex[code_off:code_off+16])
    insns = dex[code_off+16 : code_off+16+insns_size*2]
    lines = [f"      [regs={registers_size}, in={ins_size}, out={outs_size}, insns={insns_size}]"]
    pc = 0
    while pc < insns_size:
        word = struct.unpack('<H', insns[pc*2:(pc+1)*2])[0]
        op = word & 0xff
        hi = (word >> 8) & 0xff
        # Decode common Dalvik opcodes that reference strings, fields, methods, constants
        if op == 0x00 and hi == 0x00:
            lines.append(f"        {pc:04x}: nop")
            pc += 1
        elif op in (0x0e, 0x0f, 0x10, 0x11):
            names = {0x0e: 'return-void', 0x0f: f'return v{hi}', 0x10: f'return-wide v{hi}', 0x11: f'return-object v{hi}'}
            lines.append(f"        {pc:04x}: {names[op]}")
            pc += 1
        elif op == 0x12:
            vA = hi & 0xf
            lit = (hi >> 4)
            if lit >= 8: lit -= 16
            lines.append(f"        {pc:04x}: const/4 v{vA}, {lit}")
            pc += 1
        elif op == 0x13:
            lit = struct.unpack('<h', insns[(pc+1)*2:(pc+2)*2])[0]
            lines.append(f"        {pc:04x}: const/16 v{hi}, {lit}")
            pc += 2
        elif op == 0x14:
            lit = struct.unpack('<i', insns[(pc+1)*2:(pc+3)*2])[0]
            lines.append(f"        {pc:04x}: const v{hi}, {lit} (0x{lit & 0xffffffff:x})")
            pc += 3
        elif op == 0x1a:
            s_idx = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
            lines.append(f"        {pc:04x}: const-string v{hi}, {repr(strings[s_idx])}")
            pc += 2
        elif op == 0x1c:
            t_idx = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
            lines.append(f"        {pc:04x}: const-class v{hi}, {types[t_idx]}")
            pc += 2
        elif 0x52 <= op <= 0x5f:
            f_idx = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
            vA = hi & 0xf
            vB = (hi >> 4) & 0xf
            fc, ft, fn = fields[f_idx]
            opname = 'iget' if op <= 0x58 else 'iput'
            lines.append(f"        {pc:04x}: {opname}(0x{op:02x}) v{vA}, v{vB}, {fc}->{fn}:{ft}")
            pc += 2
        elif 0x60 <= op <= 0x6d:
            f_idx = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
            fc, ft, fn = fields[f_idx]
            opname = 'sget' if op <= 0x66 else 'sput'
            lines.append(f"        {pc:04x}: {opname}(0x{op:02x}) v{hi}, {fc}->{fn}:{ft}")
            pc += 2
        elif 0x6e <= op <= 0x72:
            m_idx = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
            regs_w = struct.unpack('<H', insns[(pc+2)*2:(pc+3)*2])[0]
            mc, mn, mret, mparams = methods[m_idx]
            kinds = {0x6e: 'invoke-virtual', 0x6f: 'invoke-super', 0x70: 'invoke-direct', 0x71: 'invoke-static', 0x72: 'invoke-interface'}
            lines.append(f"        {pc:04x}: {kinds[op]} {mc}->{mn}({''.join(mparams)}){mret}")
            pc += 3
        elif 0x74 <= op <= 0x78:
            m_idx = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
            mc, mn, mret, mparams = methods[m_idx]
            kinds = {0x74: 'invoke-virtual/range', 0x75: 'invoke-super/range', 0x76: 'invoke-direct/range', 0x77: 'invoke-static/range', 0x78: 'invoke-interface/range'}
            lines.append(f"        {pc:04x}: {kinds[op]} {mc}->{mn}({''.join(mparams)}){mret}")
            pc += 3
        elif op == 0x22:
            t_idx = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
            lines.append(f"        {pc:04x}: new-instance v{hi}, {types[t_idx]}")
            pc += 2
        else:
            # Handle variable length instructions properly
            # Check payload pseudo-instructions
            if word == 0x0100: # packed-switch-payload
                sz = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
                pc += 4 + sz * 2
            elif word == 0x0200: # sparse-switch-payload
                sz = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
                pc += 2 + sz * 4
            elif word == 0x0300: # fill-array-data-payload
                elem_w = struct.unpack('<H', insns[(pc+1)*2:(pc+2)*2])[0]
                sz = struct.unpack('<I', insns[(pc+2)*2:(pc+4)*2])[0]
                pc += 4 + (sz * elem_w + 1) // 2
            else:
                # Standard instruction sizes in 16-bit code units
                sz1 = {0x00,0x01,0x04,0x07,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x1d,0x1e,0x21,0x27,0x28} | set(range(0x7b, 0x90)) | set(range(0xb0, 0xd0))
                sz2 = {0x02,0x05,0x08,0x13,0x15,0x16,0x19,0x1a,0x1c,0x1f,0x20,0x22,0x23,0x29,0x2d,0x2e,0x2f,0x30,0x31} | set(range(0x32, 0x3e)) | set(range(0x44, 0x6e)) | set(range(0x90, 0xb0)) | set(range(0xd0, 0xe0))
                sz3 = {0x03,0x06,0x09,0x14,0x17,0x1b,0x24,0x25,0x26,0x2a,0x2b,0x2c} | set(range(0x6e, 0x79))
                if op in sz1:
                    pc += 1
                elif op in sz2:
                    pc += 2
                elif op in sz3:
                    pc += 3
                elif op == 0x18:
                    pc += 5
                else:
                    pc += 1
    return "\n".join(lines)

out = []
for i in range(class_defs_size):
    c_idx, access, super_idx, interfaces_off, src_file_idx, annot_off, class_data_off, static_vals_off = struct.unpack('<IIIIIIII', dex[class_defs_off + i*32 : class_defs_off + (i+1)*32])
    cname = types[c_idx]
    sname = types[super_idx] if super_idx != 0xffffffff else 'None'
    out.append(f"============================================================")
    out.append(f"CLASS [{i}] {cname} extends {sname}")
    out.append(f"============================================================")
    if class_data_off == 0:
        continue
    pos = class_data_off
    sf_size, pos = read_uleb128(dex, pos)
    if_size, pos = read_uleb128(dex, pos)
    dm_size, pos = read_uleb128(dex, pos)
    vm_size, pos = read_uleb128(dex, pos)

    f_idx = 0
    for _ in range(sf_size):
        diff, pos = read_uleb128(dex, pos)
        acc, pos = read_uleb128(dex, pos)
        f_idx += diff
        fc, ft, fn = fields[f_idx]
        out.append(f"  static field {fn}: {ft} (access=0x{acc:x})")

    f_idx = 0
    for _ in range(if_size):
        diff, pos = read_uleb128(dex, pos)
        acc, pos = read_uleb128(dex, pos)
        f_idx += diff
        fc, ft, fn = fields[f_idx]
        out.append(f"  instance field {fn}: {ft} (access=0x{acc:x})")

    m_idx = 0
    for _ in range(dm_size):
        diff, pos = read_uleb128(dex, pos)
        acc, pos = read_uleb128(dex, pos)
        code_off, pos = read_uleb128(dex, pos)
        m_idx += diff
        mc, mn, mret, mparams = methods[m_idx]
        native_tag = " [NATIVE]" if (acc & 0x100) else ""
        out.append(f"\n  direct method {mn}({''.join(mparams)}){mret}{native_tag} (access=0x{acc:x})")
        if code_off != 0:
            out.append(disassemble_code(code_off))

    m_idx = 0
    for _ in range(vm_size):
        diff, pos = read_uleb128(dex, pos)
        acc, pos = read_uleb128(dex, pos)
        code_off, pos = read_uleb128(dex, pos)
        m_idx += diff
        mc, mn, mret, mparams = methods[m_idx]
        native_tag = " [NATIVE]" if (acc & 0x100) else ""
        out.append(f"\n  virtual method {mn}({''.join(mparams)}){mret}{native_tag} (access=0x{acc:x})")
        if code_off != 0:
            out.append(disassemble_code(code_off))

with open('reference/dex_dump.txt', 'w', encoding='utf-8') as f:
    f.write("\n".join(out))
print(f"Wrote reference/dex_dump.txt ({len(out)} lines)")
