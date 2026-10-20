#!/usr/bin/env python3
"""
Heroes Lore 5 - Gameplay Tables Translation & Compiler Pipeline
Translates:
  - Quests (quest_0.dat, quest_1.dat, quest_2.dat)
  - Items & Equipment (item_00.dat .. item_18.dat)
  - Skills & Traits (skill_00.dat .. skill_05.dat)
  - Enemy Names (enemy_0.dat, enemy_1.dat, enemy_2.dat, enemy_expert_*.dat)
  - Achievement Titles (mission_list.dat)
  - Blacksmith Recipes (smith_0.dat .. smith_2.dat)
  - Socket Upgrades (buildup.dat)
  - Set Bonuses (set_option.dat)
  - Fixes GIRL -> SPIRIT and SHARGE -> CHARGE in common_text.dat
Installs directly to assets_override/c/csv/ and dist/assets_override/c/csv/.
"""

import os
import sys
import json
import glob
import time
import struct
import urllib.request
import urllib.parse
import re

DICT_PATH = "lang/dict.json"

CUSTOM_TRANSLATIONS = {
    # Quest Titles
    "지하도 청소": "Underground Cleaning",
    "메인 퀘스트": "Main Quest",
    "합류": "Rendezvous",
    "귀가": "Heading Home",
    "알찬 의뢰": "Fruitful Request",
    "탐색": "Investigation",
    "붉은머리 남자": "Red-Haired Man",
    "도주": "Escape",
    "정황": "Circumstances",
    
    # Common Skills & Traits
    "양손베기": "Two-Handed Slash",
    "돌진": "Charge",
    "내려찍기": "Smash Down",
    "어깨치기": "Shoulder Tackle",
    "올려베기": "Upward Slash",
    "회전베기": "Whirlwind Slash",
    "도발": "Taunt",
    "찌르기": "Pierce",
    "연속베기": "Consecutive Slash",
    "연속사격": "Rapid Fire",
    "창격술": "Spearmanship",
    "암흑탄": "Dark Bullet",
    "동체시력": "Kinetic Vision",
    
    # Common Items & Weapons
    "롱소드": "Longsword",
    "대거": "Dagger",
    "스피어": "Spear",
    "스태프": "Staff",
    "서클릿": "Circlet",
    "코트": "Coat",
    "글러브": "Gloves",
    "그리브": "Greaves",
    "버클러": "Buckler",
    "포션": "Potion",
    "살코기": "Meat",
    "양손검": "Two-Handed Sword",
    "한손검": "One-Handed Sword",
    "도끼": "Axe",
    "단검": "Dagger",
    "경갑": "Light Armor",
    "중갑": "Medium Armor",
    "방패": "Shield",
    "창": "Spear",
    "총": "Gun",
    
    # Enemies
    "지하 고블린": "Underground Goblin",
    "고블린": "Goblin",
    "박쥐": "Bat",
    "은폐된 포대": "Concealed Turret",
    "자동 포대": "Auto Turret",
    "고대종실험체": "Ancient Specimen",
    "흥분한 박쥐": "Enraged Bat",
    "흥분한 고블린": "Enraged Goblin",
    "정체모를 괴수": "Unknown Beast",
    "해안전갈": "Coast Scorpion",
    "말벌": "Wasp",
    "늑대": "Wolf",
    "다리 경비대": "Bridge Guard",
    "다리 경비대장": "Bridge Captain",
    "대형 경비로봇": "Heavy Guard Robot",
    "타조": "Ostrich",
    "사육된 고블린": "Tamed Goblin",
    "고대종": "Ancient Species",
    "스켈레톤": "Skeleton",
    "흑의병": "Black Guard",
    "얼음정령": "Ice Elemental",
    "물개": "Seal",
    "설녀": "Snow Maiden",
    "아이스웜": "Ice Worm",
    "악령": "Evil Spirit",
    "심해인": "Deep Sea Dweller",
    "망령기사": "Wraith Knight",
    "타락한 상자": "Corrupted Chest",
    "사신": "Reaper",
    "악몽의 잔해": "Nightmare Residue",
    "미믹": "Mimic",
    "포악한 타조": "Fierce Ostrich",
    "데몬": "Demon",
    "암모나이트": "Ammonite",
    "용암거미": "Lava Spider",
    "불의 정령": "Fire Elemental",
    "고위사도": "High Apostle"
}

def load_dict():
    d = {}
    if os.path.exists(DICT_PATH):
        try:
            with open(DICT_PATH, "r", encoding="utf-8") as f:
                d = json.load(f)
        except Exception:
            d = {}
    for k, v in CUSTOM_TRANSLATIONS.items():
        d[k] = v
    return d

def save_dict(d):
    with open(DICT_PATH, "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=2)

def query_google_translate(batch):
    if not batch:
        return []
    combined = "\n".join(batch)
    url = "https://translate.googleapis.com/translate_a/single?client=gtx&sl=ko&tl=en&dt=t&q=" + urllib.parse.quote(combined)
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"})
    try:
        with urllib.request.urlopen(req, timeout=12) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            translated_combined = "".join([part[0] for part in data[0] if part and part[0]])
            lines = translated_combined.split("\n")
            if len(lines) == len(batch):
                return [l.strip() for l in lines]
    except Exception as e:
        print(f"Batch query error ({e}), retrying single items...")
    
    results = []
    for item in batch:
        try:
            u = "https://translate.googleapis.com/translate_a/single?client=gtx&sl=ko&tl=en&dt=t&q=" + urllib.parse.quote(item)
            rq = urllib.request.Request(u, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(rq, timeout=5) as r:
                res = json.loads(r.read().decode("utf-8"))
                results.append("".join([part[0] for part in res[0] if part and part[0]]).strip())
            time.sleep(0.04)
        except Exception:
            results.append(item)
    return results

def translate_phrase_list(phrase_list, trans_dict, batch_size=40):
    needed = []
    clean_map = {}
    
    for p in phrase_list:
        if not p: continue
        if p in trans_dict: continue
        if not any("\uac00" <= c <= "\ud7a3" for c in p):
            trans_dict[p] = p
            continue
            
        m = re.match(r"^([\^\&\#\*\-\|\~\>\<\@\$\%]+)(.*)", p)
        if m:
            prefix = m.group(1)
            clean = m.group(2).strip()
        else:
            prefix = ""
            clean = p.strip()
            
        if not clean or not any("\uac00" <= c <= "\ud7a3" for c in clean):
            trans_dict[p] = p
            continue
            
        if clean in trans_dict:
            trans_dict[p] = prefix + trans_dict[clean]
            continue
            
        clean_map.setdefault(clean, []).append((p, prefix))
        if clean not in needed:
            needed.append(clean)
            
    if not needed:
        return
        
    print(f"Translating {len(needed)} unique Korean phrases for gameplay tables...")
    total_batches = (len(needed) + batch_size - 1) // batch_size
    
    for b_idx in range(0, len(needed), batch_size):
        chunk = needed[b_idx:b_idx + batch_size]
        curr_batch = (b_idx // batch_size) + 1
        print(f"  Batch {curr_batch}/{total_batches} ({len(chunk)} phrases)...", end="", flush=True)
        translated = query_google_translate(chunk)
        for orig_clean, trans_clean in zip(chunk, translated):
            trans_dict[orig_clean] = trans_clean
            for full_p, pfx in clean_map.get(orig_clean, []):
                trans_dict[full_p] = pfx + trans_clean
        print(" done.")
        save_dict(trans_dict)
        time.sleep(0.08)

def tr(text, trans_dict, max_bytes=254):
    if not text: return ""
    parts = text.split(";")
    out_parts = []
    for part in parts:
        s = part.strip()
        if not s:
            out_parts.append(part)
            continue
        m = re.match(r"^([\^\&\#\*\-\|\~\>\<\@\$\%]+)(.*)", s)
        if m:
            pfx = m.group(1)
            core = m.group(2).strip()
        else:
            pfx = ""
            core = s
        if s in trans_dict:
            out_parts.append(trans_dict[s])
        elif core in trans_dict:
            out_parts.append(pfx + trans_dict[core])
        else:
            out_parts.append(part)
    res = ";".join(out_parts)
    b = res.encode('cp949', errors='replace')
    if len(b) > max_bytes:
        res = b[:max_bytes].decode('cp949', errors='ignore')
    return res

def write_dual(rel_path, data):
    for base in ["assets_override", "dist/assets_override"]:
        target = os.path.join(base, rel_path)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as f:
            f.write(data)

def run():
    trans_dict = load_dict()
    all_phrases = set()
    
    # 1. Quests phrases
    print("Collecting Quest phrases...")
    for qfn in ['quest_0.dat', 'quest_1.dat', 'quest_2.dat']:
        d = open(f'reference/extracted_vfs/c/csv/{qfn}', 'rb').read()
        cnt = struct.unpack('<H', d[:2])[0]
        pos = 2
        for _ in range(cnt):
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            tlen = ebytes[3]
            title = ebytes[4:4+tlen].decode('cp949', errors='replace')
            bpos = 4 + tlen
            blen = ebytes[bpos]
            body = ebytes[bpos+1:bpos+1+blen].decode('cp949', errors='replace')
            rem = ebytes[bpos+1+blen:]
            s3len = rem[0]
            s3 = rem[1:1+s3len].decode('cp949', errors='replace')
            for s in [title, s3]:
                if s.strip(): all_phrases.add(s.strip())
            for sub in body.split(';'):
                if sub.strip(): all_phrases.add(sub.strip())
            pos += 2 + entry_sz

    # 2. Items phrases
    print("Collecting Items phrases...")
    for f in sorted(glob.glob('reference/extracted_vfs/c/csv/item_*.dat')):
        d = open(f, 'rb').read()
        if len(d) < 2: continue
        cnt = struct.unpack('<H', d[:2])[0]
        pos = 2
        for _ in range(cnt):
            if pos + 2 > len(d): break
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            nlen = ebytes[2]
            name = ebytes[3:3+nlen].decode('cp949', errors='replace')
            cur = 3 + nlen + 4
            dlen = ebytes[cur]
            desc = ebytes[cur+1:cur+1+dlen].decode('cp949', errors='replace')
            if name.strip(): all_phrases.add(name.strip())
            for sub in desc.split(';'):
                if sub.strip(): all_phrases.add(sub.strip())
            pos += 2 + entry_sz

    # 3. Skills phrases
    print("Collecting Skills phrases...")
    for f in sorted(glob.glob('reference/extracted_vfs/c/csv/skill_*.dat')):
        d = open(f, 'rb').read()
        cnt = struct.unpack('<H', d[:2])[0]
        pos = 2
        for _ in range(cnt):
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            nlen = ebytes[0]
            name = ebytes[1:1+nlen].decode('cp949', errors='replace')
            desc = ebytes[1+nlen+48:].decode('cp949', errors='replace')
            if name.strip(): all_phrases.add(name.strip())
            for sub in desc.split(';'):
                if sub.strip(): all_phrases.add(sub.strip())
            pos += 2 + entry_sz

    # 4. Enemies phrases
    print("Collecting Enemies phrases...")
    for f in sorted(glob.glob('reference/extracted_vfs/c/csv/enemy_*.dat')):
        if 'enemy_g' in f: continue
        d = open(f, 'rb').read()
        cnt = struct.unpack('<H', d[:2])[0]
        pos = 2
        for _ in range(cnt):
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            nlen = ebytes[0]
            name = ebytes[1:1+nlen].decode('cp949', errors='replace')
            if name.strip(): all_phrases.add(name.strip())
            pos += 2 + entry_sz

    # 5. Mission list phrases
    print("Collecting Mission list phrases...")
    d = open('reference/extracted_vfs/c/csv/mission_list.dat', 'rb').read()
    cnt = struct.unpack('<H', d[:2])[0]
    pos = 2
    for _ in range(cnt):
        entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
        ebytes = d[pos+2:pos+2+entry_sz]
        nlen = ebytes[0]
        name = ebytes[1:1+nlen].decode('cp949', errors='replace')
        if name.strip(): all_phrases.add(name.strip())
        pos += 2 + entry_sz

    # 6. Smith recipes phrases
    print("Collecting Smith recipes phrases...")
    for f in sorted(glob.glob('reference/extracted_vfs/c/csv/smith_*.dat')):
        d = open(f, 'rb').read()
        cnt = struct.unpack('<H', d[:2])[0]
        pos = 2
        for _ in range(cnt):
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            nlen = ebytes[2]
            name = ebytes[3:3+nlen].decode('cp949', errors='replace')
            cur = 3 + nlen + 4
            mlen = ebytes[cur]
            mat = ebytes[cur+1:cur+1+mlen].decode('cp949', errors='replace')
            if name.strip(): all_phrases.add(name.strip())
            if mat.strip(): all_phrases.add(mat.strip())
            pos += 2 + entry_sz

    # 7. Buildup & set_option phrases
    for fn in ['buildup.dat', 'set_option.dat']:
        d = open(f'reference/extracted_vfs/c/csv/{fn}', 'rb').read()
        cnt = struct.unpack('<H', d[:2])[0]
        pos = 2
        for _ in range(cnt):
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            nlen = ebytes[0]
            name = ebytes[1:1+nlen].decode('cp949', errors='replace')
            if name.strip(): all_phrases.add(name.strip())
            pos += 2 + entry_sz

    korean_phrases = [p for p in all_phrases if any('\uac00' <= c <= '\ud7a3' for c in p)]
    print(f"Total unique phrases in gameplay tables: {len(korean_phrases)}")
    translate_phrase_list(korean_phrases, trans_dict)
    save_dict(trans_dict)

    # ---------------------------------------------------------
    # Recompile Quests
    # ---------------------------------------------------------
    for qfn in ['quest_0.dat', 'quest_1.dat', 'quest_2.dat']:
        d = open(f'reference/extracted_vfs/c/csv/{qfn}', 'rb').read()
        cnt = struct.unpack('<H', d[:2])[0]
        out = bytearray(struct.pack('<H', cnt))
        pos = 2
        for i in range(cnt):
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            pfx = ebytes[:3]
            tlen = ebytes[3]
            orig_t = ebytes[4:4+tlen].decode('cp949', errors='replace')
            bpos = 4 + tlen
            blen = ebytes[bpos]
            orig_b = ebytes[bpos+1:bpos+1+blen].decode('cp949', errors='replace')
            rem = ebytes[bpos+1+blen:]
            s3len = rem[0]
            orig_s3 = rem[1:1+s3len].decode('cp949', errors='replace')
            footer = rem[1+s3len:]
            
            en_t = tr(orig_t, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            en_b = tr(orig_b, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            en_s3 = tr(orig_s3, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            
            entry = pfx + bytes([len(en_t)]) + en_t + bytes([len(en_b)]) + en_b + bytes([len(en_s3)]) + en_s3 + footer
            out += struct.pack('<H', len(entry))
            out += entry
            pos += 2 + entry_sz
            
        write_dual(f"c/csv/{qfn}", bytes(out))
        print(f"Compiled {qfn} ({cnt} quests, {len(out)} bytes)")

    # ---------------------------------------------------------
    # Recompile Items
    # ---------------------------------------------------------
    for f in sorted(glob.glob('reference/extracted_vfs/c/csv/item_*.dat')):
        bn = os.path.basename(f)
        d = open(f, 'rb').read()
        if len(d) < 2: continue
        cnt = struct.unpack('<H', d[:2])[0]
        out = bytearray(struct.pack('<H', cnt))
        pos = 2
        for i in range(cnt):
            if pos + 2 > len(d): break
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            id_bytes = ebytes[:2]
            nlen = ebytes[2]
            orig_name = ebytes[3:3+nlen].decode('cp949', errors='replace')
            cur = 3 + nlen
            price_bytes = ebytes[cur:cur+4]
            cur += 4
            dlen = ebytes[cur]
            orig_desc = ebytes[cur+1:cur+1+dlen].decode('cp949', errors='replace')
            cur += 1 + dlen
            rem = ebytes[cur:]
            
            en_name = tr(orig_name, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            en_desc = tr(orig_desc, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            
            entry = id_bytes + bytes([len(en_name)]) + en_name + price_bytes + bytes([len(en_desc)]) + en_desc + rem
            out += struct.pack('<H', len(entry))
            out += entry
            pos += 2 + entry_sz
            
        write_dual(f"c/csv/{bn}", bytes(out))
        print(f"Compiled {bn} ({cnt} items, {len(out)} bytes)")

    # ---------------------------------------------------------
    # Recompile Skills
    # ---------------------------------------------------------
    for f in sorted(glob.glob('reference/extracted_vfs/c/csv/skill_*.dat')):
        bn = os.path.basename(f)
        d = open(f, 'rb').read()
        cnt = struct.unpack('<H', d[:2])[0]
        out = bytearray(struct.pack('<H', cnt))
        pos = 2
        for i in range(cnt):
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            nlen = ebytes[0]
            orig_name = ebytes[1:1+nlen].decode('cp949', errors='replace')
            attrs = ebytes[1+nlen:1+nlen+47]
            orig_desc = ebytes[1+nlen+48:].decode('cp949', errors='replace')
            
            en_name = tr(orig_name, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            en_desc = tr(orig_desc, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            
            entry = bytes([len(en_name)]) + en_name + attrs + bytes([len(en_desc)]) + en_desc
            out += struct.pack('<H', len(entry))
            out += entry
            pos += 2 + entry_sz
            
        write_dual(f"c/csv/{bn}", bytes(out))
        print(f"Compiled {bn} ({cnt} skills, {len(out)} bytes)")

    # ---------------------------------------------------------
    # Recompile Enemies
    # ---------------------------------------------------------
    for f in sorted(glob.glob('reference/extracted_vfs/c/csv/enemy_*.dat')):
        if 'enemy_g' in f: continue
        bn = os.path.basename(f)
        d = open(f, 'rb').read()
        cnt = struct.unpack('<H', d[:2])[0]
        out = bytearray(struct.pack('<H', cnt))
        pos = 2
        for i in range(cnt):
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            nlen = ebytes[0]
            orig_name = ebytes[1:1+nlen].decode('cp949', errors='replace')
            stats = ebytes[1+nlen:]
            
            en_name = tr(orig_name, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            entry = bytes([len(en_name)]) + en_name + stats
            out += struct.pack('<H', len(entry))
            out += entry
            pos += 2 + entry_sz
            
        write_dual(f"c/csv/{bn}", bytes(out))
        print(f"Compiled {bn} ({cnt} enemies, {len(out)} bytes)")

    # ---------------------------------------------------------
    # Recompile Mission List
    # ---------------------------------------------------------
    d = open('reference/extracted_vfs/c/csv/mission_list.dat', 'rb').read()
    cnt = struct.unpack('<H', d[:2])[0]
    out = bytearray(struct.pack('<H', cnt))
    pos = 2
    for i in range(cnt):
        entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
        ebytes = d[pos+2:pos+2+entry_sz]
        nlen = ebytes[0]
        orig_name = ebytes[1:1+nlen].decode('cp949', errors='replace')
        stats = ebytes[1+nlen:]
        
        en_name = tr(orig_name, trans_dict, max_bytes=254).encode('cp949', errors='replace')
        entry = bytes([len(en_name)]) + en_name + stats
        out += struct.pack('<H', len(entry))
        out += entry
        pos += 2 + entry_sz
    write_dual("c/csv/mission_list.dat", bytes(out))
    print(f"Compiled mission_list.dat ({cnt} achievement titles, {len(out)} bytes)")

    # ---------------------------------------------------------
    # Recompile Smith Recipes
    # ---------------------------------------------------------
    for f in sorted(glob.glob('reference/extracted_vfs/c/csv/smith_*.dat')):
        bn = os.path.basename(f)
        d = open(f, 'rb').read()
        cnt = struct.unpack('<H', d[:2])[0]
        out = bytearray(struct.pack('<H', cnt))
        pos = 2
        for i in range(cnt):
            entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
            ebytes = d[pos+2:pos+2+entry_sz]
            id_bytes = ebytes[:2]
            nlen = ebytes[2]
            orig_name = ebytes[3:3+nlen].decode('cp949', errors='replace')
            cur = 3 + nlen
            pfx = ebytes[cur:cur+4]
            cur += 4
            mlen = ebytes[cur]
            orig_mat = ebytes[cur+1:cur+1+mlen].decode('cp949', errors='replace')
            cur += 1 + mlen
            rest = ebytes[cur:]
            
            en_name = tr(orig_name, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            en_mat = tr(orig_mat, trans_dict, max_bytes=254).encode('cp949', errors='replace')
            
            entry = id_bytes + bytes([len(en_name)]) + en_name + pfx + bytes([len(en_mat)]) + en_mat + rest
            out += struct.pack('<H', len(entry))
            out += entry
            pos += 2 + entry_sz
            
        write_dual(f"c/csv/{bn}", bytes(out))
        print(f"Compiled {bn} ({cnt} recipes, {len(out)} bytes)")

    # ---------------------------------------------------------
    # Recompile Buildup & Set Option
    # ---------------------------------------------------------
    d = open('reference/extracted_vfs/c/csv/buildup.dat', 'rb').read()
    cnt = struct.unpack('<H', d[:2])[0]
    out = bytearray(struct.pack('<H', cnt))
    pos = 2
    for i in range(cnt):
        entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
        ebytes = d[pos+2:pos+2+entry_sz]
        nlen = ebytes[0]
        orig_name = ebytes[1:1+nlen].decode('cp949', errors='replace')
        rest = ebytes[1+nlen:]
        en_name = tr(orig_name, trans_dict, max_bytes=254).encode('cp949', errors='replace')
        entry = bytes([len(en_name)]) + en_name + rest
        out += struct.pack('<H', len(entry))
        out += entry
        pos += 2 + entry_sz
    write_dual("c/csv/buildup.dat", bytes(out))
    print(f"Compiled buildup.dat ({cnt} gems, {len(out)} bytes)")

    d = open('reference/extracted_vfs/c/csv/set_option.dat', 'rb').read()
    cnt = struct.unpack('<H', d[:2])[0]
    out = bytearray(struct.pack('<H', cnt))
    pos = 2
    for i in range(cnt):
        entry_sz = struct.unpack('<H', d[pos:pos+2])[0]
        ebytes = d[pos+2:pos+2+entry_sz]
        nlen = ebytes[0]
        orig_name = ebytes[1:1+nlen].decode('cp949', errors='replace')
        rest = ebytes[1+nlen:]
        en_name = tr(orig_name, trans_dict, max_bytes=254).encode('cp949', errors='replace')
        entry = bytes([len(en_name)]) + en_name + rest
        out += struct.pack('<H', len(entry))
        out += entry
        pos += 2 + entry_sz
    write_dual("c/csv/set_option.dat", bytes(out))
    print(f"Compiled set_option.dat ({cnt} set bonuses, {len(out)} bytes)")

    # ---------------------------------------------------------
    # Fix common_text.json: GIRL -> SPIRIT and SHARGE -> CHARGE
    # ---------------------------------------------------------
    ct_path = "lang/en/common_text.json"
    if os.path.exists(ct_path):
        with open(ct_path, "r", encoding="utf-8") as f:
            ct = json.load(f)
        for item in ct:
            if item["id"] == 5:
                item["text"] = "CHARGE"
            elif item["id"] == 6:
                item["text"] = "SPIRIT"
            elif item["id"] == 231:
                item["text"] = "Spirit/Magic Def."
            elif item["id"] == 251:
                item["text"] = "The spirit does not have enough energy to attack."
        with open(ct_path, "w", encoding="utf-8") as f:
            json.dump(ct, f, ensure_ascii=False, indent=2)
            
        # Rebuild common_text.dat
        out = bytearray(struct.pack('<H', len(ct)))
        for item in ct:
            text = item["text"]
            s_bytes = text.encode('cp949', errors='replace')
            if len(s_bytes) > 255:
                s_bytes = s_bytes[:255]
            buf_sz = len(s_bytes) + 1
            out += struct.pack('<H', buf_sz)
            out.append(len(s_bytes))
            out += s_bytes
        write_dual("c/csv/common_text.dat", bytes(out))
        print("Updated common_text.dat: SHARGE -> CHARGE, GIRL -> SPIRIT, and Spirit descriptions fixed!")

    print("\n[SUCCESS] All gameplay databases translated and deployed to assets_override/ and dist/assets_override/!")

if __name__ == "__main__":
    run()
