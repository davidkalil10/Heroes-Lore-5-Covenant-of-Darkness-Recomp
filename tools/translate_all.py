#!/usr/bin/env python3
"""
Heroes Lore 5 - Complete Translation & Asset Compiler Pipeline
Translates Menus, In-Game Text, Mission Text, Help Data, and All Dialogue Scenarios (.scn),
and compiles them directly into assets_override/ and dist/assets_override/.
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

SEEDS = {
    # Characters & Classes
    "슈르츠": "Schulz",
    "티르": "Tyr",
    "크래드": "Crad",
    "네베드": "Neved",
    "루퍼스": "Rufus",
    "에블린": "Evelyn",
    "아일린": "Aileen",
    "알렉스": "Alex",
    "카즈": "Kaz",
    "크리스": "Chris",
    "알테나": "Althena",
    "루레인": "Rurain",
    "티아나": "Tiana",
    "워리어": "Warrior",
    "로그": "Rogue",
    "건슬링어": "Gunslinger",
    "나이트": "Knight",
    
    # Core Menus
    "데이터 관리": "Data Management",
    "데이터 저장하기": "Save Game",
    "창고관리": "Storage",
    "창고": "Storage",
    "싱글가방": "Inventory",
    "우편함": "Mailbox",
    "슬롯변경": "Change Slot",
    "캐쉬상점": "Shop",
    "판매거래소": "Marketplace (Sell)",
    "구매거래소": "Marketplace (Buy)",
    "판매중": "Selling",
    "판매완료": "Sold",
    "기간만료": "Expired",
    "신규등록": "Register",
    "대금회수": "Collect Gold",
    "등록취소": "Cancel Listing",
    "이름검색": "Search Name",
    "조건검색": "Filter Search",
    "아이템 이름을 입력하세요": "Enter item name",
    "검색하기": "Search",
    "전체": "All",
    "무기": "Weapons",
    "방어구": "Armor",
    "조합서": "Recipes",
    "스킬북": "Skill Books",
    "기타": "Misc",
    "양손검/도끼": "Two-Handed / Axe",
    "한손검/단검": "One-Handed / Dagger",
    "건": "Guns",
    "창": "Spears",
    "투구": "Helmets",
    "갑옷": "Body Armor",
    "장갑": "Gloves",
    "신발": "Boots",
    "방패": "Shields",
    "장신구": "Accessories",
    "소모품": "Consumables",
    "오브": "Orbs",
    "재료": "Materials",
    "워리어/로그": "Warrior / Rogue",
    "건슬링어/나이트": "Gunslinger / Knight",
    "노멀": "Normal",
    "레어": "Rare",
    "에픽": "Epic",
    "영웅": "Heroic",
    "전설": "Legendary",
    "세트": "Set",
    "예": "Yes",
    "아니오": "No",
    "완료": "Complete",
    "만료": "Expired",
    "무한": "Infinite",
    "없음": "None",
    "입력": "Enter",
    "삭제": "Delete",
    "버리기": "Discard",
    "내리기": "Take Out",
    "올리기": "Store",
    "전송하기": "Send",
    "답장": "Reply",
    "수락": "Accept",
    "반송": "Return",
    "아무키나 누르세요": "Press Any Key",
    "아무데나 터치하세요": "Touch Anywhere",
    
    # Help Categories
    "기본 조작": "Basic Controls",
    "기본 인터페이스": "User Interface",
    "클래스/워리어": "Class: Warrior",
    "클래스/로그": "Class: Rogue",
    "클래스/건슬링어": "Class: Gunslinger",
    "클래스/나이트": "Class: Knight",
    "캐릭터/능력치": "Stats & Attributes",
    "캐릭터/스킬": "Skills",
    "캐릭터/연계기": "Combos",
    "캐릭터/특성": "Traits & Talents",
    "장비": "Equipment",
    "정령/기본정보": "Spirits: Overview",
    "정령/스킬": "Spirits: Skills",
    "아이템 조합": "Item Crafting",
    "퀘스트": "Quests",
    "익스퍼트": "Expert Mode",
    "달성과제": "Achievements",
    "창고접속": "Online Storage",
    "게임문의": "Support",
    "기타": "Other",

    # Key In-Game Phrases
    "센트럴, 지하도": "Central, Underground",
    "센트럴 타워": "Central Tower",
    "황혼의 다리": "Twilight Bridge",
    "고대종": "Ancient Species",
    "고블린": "Goblin",
    "스킬을 배우시겠습니까?": "Learn this skill?",
    "퀘스트를 포기하였습니다.": "Abandoned the quest.",
    "퀘스트완료": "Quest Complete",
    "을(를) 구매하였습니다.": "Purchased.",
    "을(를) 사용하였습니다.": "Used.",
    "을(를) 획득하였습니다.": "Obtained."
}

def load_dict():
    os.makedirs(os.path.dirname(DICT_PATH), exist_ok=True)
    d = {}
    if os.path.exists(DICT_PATH):
        try:
            with open(DICT_PATH, "r", encoding="utf-8") as f:
                d = json.load(f)
        except Exception:
            d = {}
    for k, v in SEEDS.items():
        if k not in d:
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
            time.sleep(0.05)
        except Exception:
            results.append(item)
    return results

def translate_phrase_list(phrase_list, trans_dict, batch_size=40):
    needed = []
    clean_map = {}
    
    for p in phrase_list:
        if not p:
            continue
        if p in trans_dict:
            continue
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
        
    print(f"Translating {len(needed)} unique Korean phrases...")
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
        time.sleep(0.1)

def translate_compound_text(text, trans_dict):
    if not text:
        return text
    parts = text.split(";")
    trans_parts = []
    for part in parts:
        s = part.strip()
        if not s:
            trans_parts.append(part)
            continue
        m = re.match(r"^([\^\&\#\*\-\|\~\>\<\@\$\%]+)(.*)", s)
        if m:
            prefix = m.group(1)
            core = m.group(2).strip()
        else:
            prefix = ""
            core = s
        
        if s in trans_dict:
            trans_parts.append(trans_dict[s])
        elif core in trans_dict:
            trans_parts.append(prefix + trans_dict[core])
        else:
            trans_parts.append(part)
    return ";".join(trans_parts)

# -------------------------------------------------------------
# Binary Parsers & Builders
# -------------------------------------------------------------

def parse_simple_text_dat(data):
    cnt = struct.unpack('<H', data[:2])[0]
    pos = 2
    items = []
    for i in range(cnt):
        buf_sz = struct.unpack('<H', data[pos:pos+2])[0]
        str_len = data[pos+2]
        s_bytes = data[pos+3:pos+3+str_len]
        pos += 2 + 1 + str_len
        try:
            s_txt = s_bytes.decode('cp949', errors='replace')
        except Exception:
            s_txt = s_bytes.decode('latin1')
        items.append({"id": i, "text": s_txt})
    return items

def build_simple_text_dat(items, encoding='cp949'):
    out = bytearray(struct.pack('<H', len(items)))
    for item in items:
        text = item["text"]
        s_bytes = text.encode(encoding, errors='replace')
        if len(s_bytes) > 255:
            s_bytes = s_bytes[:255]
        buf_sz = len(s_bytes) + 1
        out += struct.pack('<H', buf_sz)
        out.append(len(s_bytes))
        out += s_bytes
    return bytes(out)

def parse_help_dat(data):
    cnt = struct.unpack('<H', data[:2])[0]
    pos = 2
    entries = []
    for _ in range(cnt):
        entry_sz = struct.unpack('<H', data[pos:pos+2])[0]
        title_len = struct.unpack('<H', data[pos+2:pos+4])[0]
        title = data[pos+4:pos+4+title_len].decode('cp949', errors='replace')
        bpos = pos + 4 + title_len
        body_len = struct.unpack('<H', data[bpos:bpos+2])[0]
        body = data[bpos+2:bpos+2+body_len].decode('cp949', errors='replace')
        entries.append({'title': title, 'body': body})
        pos += 2 + entry_sz
    return entries

def build_help_dat(entries, encoding='cp949'):
    out = bytearray(struct.pack('<H', len(entries)))
    for e in entries:
        t_bytes = e['title'].encode(encoding, errors='replace')
        b_bytes = e['body'].encode(encoding, errors='replace')
        entry_sz = 2 + len(t_bytes) + 2 + len(b_bytes)
        out += struct.pack('<H', entry_sz)
        out += struct.pack('<H', len(t_bytes))
        out += t_bytes
        out += struct.pack('<H', len(b_bytes))
        out += b_bytes
    return bytes(out)

def parse_scn_file(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) == 0:
        return b'', []
    for count_pos in range(len(data) - 2, -1, -1):
        n = data[count_pos]
        if n == 0 or count_pos + 1 + n > len(data):
            continue
        lengths = list(data[count_pos+1 : count_pos+1+n])
        if count_pos + 1 + n + sum(lengths) == len(data):
            cur = count_pos + 1 + n
            valid = True
            strings = []
            for l in lengths:
                if l <= 0 or data[cur + l - 1] != 0:
                    valid = False
                    break
                strings.append(data[cur : cur + l - 1].decode('cp949', errors='replace'))
                cur += l
            if valid:
                return data[:count_pos], strings
    if len(data) >= 1 and data[-1] == 0:
        return data, []
    return b'', []

def build_scn(code, strings, encoding='cp949'):
    out = bytearray(code)
    out.append(len(strings))
    encoded = []
    for s in strings:
        b = s.encode(encoding, errors='replace')
        if len(b) > 254:
            b = b[:254]
        encoded.append(b + b'\x00')
    for s in encoded:
        out.append(len(s))
    for s in encoded:
        out += s
    return bytes(out)

# -------------------------------------------------------------
# Main Translation & Pipeline Execution
# -------------------------------------------------------------

def run_pipeline():
    trans_dict = load_dict()
    
    # 1. Collect all phrases from UI simple files
    ui_files = ["menu_text.json", "ingame_text.json", "mission_text.json"]
    all_phrases = set()
    for uif in ui_files:
        p = os.path.join("lang/source_ko", uif)
        if os.path.exists(p):
            with open(p, "r", encoding="utf-8") as f:
                items = json.load(f)
            for it in items:
                for sub in it["text"].split(";"):
                    s = sub.strip()
                    if s and any("\uac00" <= c <= "\ud7a3" for c in s):
                        all_phrases.add(s)
                        
    # 2. Collect phrases from help dat
    help_path = "reference/extracted_vfs/c/csv2/help_android_skt.dat"
    if os.path.exists(help_path):
        with open(help_path, "rb") as f:
            help_entries = parse_help_dat(f.read())
        for e in help_entries:
            if e["title"].strip(): all_phrases.add(e["title"].strip())
            for sub in e["body"].split(";"):
                s = sub.strip()
                if s and any("\uac00" <= c <= "\ud7a3" for c in s):
                    all_phrases.add(s)
                    
    # 3. Collect phrases from all .scn files
    scn_paths = sorted(glob.glob("reference/extracted_vfs/**/*.scn", recursive=True))
    print(f"Scanning {len(scn_paths)} scenario files for dialogues...")
    scn_data = {}
    for p in scn_paths:
        code, strings = parse_scn_file(p)
        scn_data[p] = (code, strings)
        for s in strings:
            for sub in s.split(";"):
                txt = sub.strip()
                if txt and any("\uac00" <= c <= "\ud7a3" for c in txt):
                    all_phrases.add(txt)
                    
    print(f"Total unique phrases: {len(all_phrases)} (Cached in dict: {len(trans_dict)})")
    
    # Run batch translation if anything is missing
    translate_phrase_list(list(all_phrases), trans_dict)
    save_dict(trans_dict)
    
    # 4. Compile UI text files to lang/en and assets_override/c/csv/
    os.makedirs("lang/en", exist_ok=True)
    os.makedirs("assets_override/c/csv", exist_ok=True)
    os.makedirs("dist/assets_override/c/csv", exist_ok=True)
    
    for uif in ["menu_text.json", "ingame_text.json", "mission_text.json", "common_text.json", "name.json", "tips.json"]:
        src_path = os.path.join("lang/source_ko", uif)
        if not os.path.exists(src_path):
            continue
        with open(src_path, "r", encoding="utf-8") as f:
            src_items = json.load(f)
            
        en_json_path = os.path.join("lang/en", uif)
        en_items = []
        if os.path.exists(en_json_path) and uif in ["common_text.json", "name.json", "tips.json"]:
            with open(en_json_path, "r", encoding="utf-8") as f:
                en_items = json.load(f)
        else:
            for it in src_items:
                en_items.append({
                    "id": it["id"],
                    "text": translate_compound_text(it["text"], trans_dict)
                })
            with open(en_json_path, "w", encoding="utf-8") as f:
                json.dump(en_items, f, ensure_ascii=False, indent=2)
                
        dat_name = uif.replace(".json", ".dat")
        bin_dat = build_simple_text_dat(en_items)
        for out_base in ["assets_override", "dist/assets_override"]:
            target_p = os.path.join(out_base, "c", "csv", dat_name)
            with open(target_p, "wb") as f:
                f.write(bin_dat)
        print(f"Compiled {uif} -> c/csv/{dat_name} ({len(en_items)} strings, {len(bin_dat)} bytes)")

    # 5. Compile Help files
    os.makedirs("assets_override/c/csv2", exist_ok=True)
    os.makedirs("dist/assets_override/c/csv2", exist_ok=True)
    if os.path.exists(help_path):
        translated_help = []
        for e in help_entries:
            translated_help.append({
                "title": trans_dict.get(e["title"].strip(), e["title"]),
                "body": translate_compound_text(e["body"], trans_dict)
            })
        with open("lang/en/help_android.json", "w", encoding="utf-8") as f:
            json.dump(translated_help, f, ensure_ascii=False, indent=2)
        help_bin = build_help_dat(translated_help)
        for help_fn in ["help_android_skt.dat", "help_android_ktf.dat", "help_android_lgt.dat", "help_skt.dat", "help_ktf.dat", "help_lgt.dat"]:
            for out_base in ["assets_override", "dist/assets_override"]:
                with open(os.path.join(out_base, "c", "csv2", help_fn), "wb") as f:
                    f.write(help_bin)
        print(f"Compiled {len(translated_help)} Help entries to c/csv2/*.dat ({len(help_bin)} bytes)")
        
    # 6. Compile all .scn files
    compiled_scn_count = 0
    for p, (code, strings) in scn_data.items():
        rel_path = os.path.relpath(p, "reference/extracted_vfs").replace("\\", "/")
        en_strings = [translate_compound_text(s, trans_dict) for s in strings]
        scn_bin = build_scn(code, en_strings)
        
        for out_base in ["assets_override", "dist/assets_override"]:
            target_p = os.path.join(out_base, rel_path)
            os.makedirs(os.path.dirname(target_p), exist_ok=True)
            with open(target_p, "wb") as f:
                f.write(scn_bin)
        compiled_scn_count += 1
        
    print(f"Successfully compiled {compiled_scn_count} .scn files with full English localization!")
    print("All assets installed in assets_override/ and dist/assets_override/.")

if __name__ == "__main__":
    run_pipeline()
