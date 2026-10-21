"""Build the PT-BR runtime dictionary without changing canonical English assets."""
import argparse
import concurrent.futures
import json
from pathlib import Path
import re
import threading
import time
import urllib.parse
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'lang/pt_br/phrases.json'
SEEDS={
 'SETTINGS':'CONFIGURAÇÕES','APP UPDATE':'ATUALIZAÇÃO DO APP','CLOUD SAVE - GOOGLE DRIVE':'SAVES NA NUVEM - GOOGLE DRIVE',
 'Game settings':'Opções do jogo','Back to game':'Voltar ao jogo','Back':'Voltar','Cancel':'Cancelar','Close':'Fechar',
 'Check for updates':'Verificar atualizações','Cloud save - Google Drive':'Saves na nuvem - Google Drive',
 'Connect Google Drive':'Conectar ao Google Drive','Disconnect Google Drive':'Desconectar Google Drive',
 'Refresh cloud backup':'Consultar backup na nuvem','Upload saved progress':'Enviar progresso salvo',
 'Restore cloud backup':'Restaurar backup da nuvem','Install update':'Instalar atualização','Check again':'Verificar novamente',
 'Please wait...':'Aguarde...','Cancel login':'Cancelar login','Language: English':'Idioma: Português (Brasil)',
 'Confirm upload - replace cloud backup':'Confirmar envio e substituir backup','Confirm restore and restart':'Confirmar restauração',
 'Disconnect this device':'Desconectar este aparelho','Cloud backup found.':'Backup encontrado na nuvem.',
 'Google Drive connected.':'Google Drive conectado.','Aspect ratio: auto':'Proporção de tela: automática',
 'Aspect ratio: native':'Proporção de tela: original','Aspect ratio: wide':'Proporção de tela: widescreen',
 'Check updates at startup: ON':'Verificar atualizações ao abrir: SIM','Check updates at startup: OFF':'Verificar atualizações ao abrir: NÃO',
 'Warrior':'Guerreiro','Knight':'Cavaleiro','Rogue':'Ladino','Gunslinger':'Atirador','Killer':'Ladino','Gunner':'Atirador',
 'Yes':'Sim','No':'Não','Save Game':'Salvar jogo','New Game':'Novo jogo','Continue':'Continuar','Load Game':'Carregar jogo',
 'STATUS':'ATRIBUTOS','Status':'Atributos','Inventory':'Inventário','INVENTORY':'INVENTÁRIO','Skills':'Habilidades',
 'SKILL':'HABILIDADE','Quest':'Missão','Quests':'Missões','Settings':'Configurações','Options':'Opções','Help':'Ajuda',
 'Attack':'Ataque','Defense':'Defesa','Level':'Nível','Equipment':'Equipamento','Equipped':'Equipado','Item':'Item',
 'Strength':'Força','Agility':'Agilidade','Intelligence':'Inteligência','Vitality':'Vitalidade','Spirit':'Espírito','SPIRIT':'ESPÍRITO',
 'Map':'Mapa','Shop':'Loja','Storage':'Armazém','Exit':'Sair','Confirm':'Confirmar','Delete':'Excluir','Buy':'Comprar','Sell':'Vender',
 'Schulz':'Schulz','Tyr':'Tyr','Crad':'Crad','Neved':'Neved','Rufus':'Rufus','Evelyn':'Evelyn','Aileen':'Aileen',
 'Alex':'Alex','Kaz':'Kaz','Chris':'Chris','Althena':'Althena','Rurain':'Rurain','Tiana':'Tiana',
 'HP':'HP','MP':'MP','EXP':'EXP','Lv':'Nv','None':'Nenhum','Normal':'Normal','Rare':'Raro','Epic':'Épico','Legendary':'Lendário',
 'Tap to continue':'Toque para continuar','TAP TO CONTINUE':'TOQUE PARA CONTINUAR','Press Any Key':'Pressione qualquer tecla',
 'Google Drive connected. Save in the game before uploading.':'Google Drive conectado. Salve no jogo antes de enviar.',
 'Save restored! Returning to title...':'Save restaurado! Voltando ao título...',
 'Arrows / D-pad: move     Confirm: select     Back: close':'Setas / direcional: navegar     Confirmar: escolher     Voltar: fechar',
}

def strings(value):
    if isinstance(value,str):yield value
    elif isinstance(value,dict):
        for v in value.values():yield from strings(v)
    elif isinstance(value,list):
        for v in value:yield from strings(v)

def collect():
    source=set(json.loads((ROOT/'lang/dict.json').read_text(encoding='utf-8')).values())
    for file in (ROOT/'lang/en').glob('*.json'):source.update(strings(json.loads(file.read_text(encoding='utf-8'))))
    for file in (ROOT/'assets_override').rglob('*'):
        if file.suffix not in ('.dat','.scn'):continue
        for data in re.findall(rb'[\x20-\x7e]{3,}',file.read_bytes()):
            text=data.decode('ascii')
            if re.search('[A-Za-z]{3}',text) and len(text)<2000:source.add(text)
    for name in ('service_ui.cpp','services.cpp'):
        text=(ROOT/'src'/name).read_text(encoding='utf-8')
        for phrase in re.findall(r'"([^"\n\\]+)"',text):
            if re.search(r'[A-Za-z]+ [A-Za-z]',phrase) and not any(x in phrase for x in ('https:', '#include','std::','/','_')):source.add(phrase)
    source.update(SEEDS)
    phrases=set()
    for text in source:
        for segment in text.split(';'):
            clean=re.sub(r'^[^A-Za-z0-9]+','',segment.strip())
            if re.search('[A-Za-z]{2}',clean):phrases.add(clean)
    return source,phrases

def request(batch):
    protected=[];tokens=[]
    for phrase in batch:
        values=[]
        def protect(m):values.append(m[0]);return f'QZX{len(values)-1}XQ'
        protected.append(re.sub(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[hlz]*[diuoxXfgesc%]',protect,phrase))
        tokens.append(values)
    combined='\n'.join(protected)
    url='https://translate.googleapis.com/translate_a/single?client=gtx&sl=en&tl=pt&dt=t&q='+urllib.parse.quote(combined)
    for retry in range(4):
        try:
            req=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0'})
            with urllib.request.urlopen(req,timeout=25) as response:data=json.load(response)
            translated=''.join(part[0] for part in data[0] if part and part[0]).split('\n')
            if len(translated)!=len(batch):raise ValueError('Translation batch line count changed')
            out=[]
            for line,values in zip(translated,tokens):
                for i,value in enumerate(values):
                    marker=f'QZX{i}XQ'
                    if marker not in line:raise ValueError('Translation changed a format token')
                    line=line.replace(marker,value)
                out.append(line.strip())
            return dict(zip(batch,out))
        except Exception:
            if len(batch)>1:
                middle=len(batch)//2
                return {**request(batch[:middle]),**request(batch[middle:])}
            time.sleep(1+retry*2)
    raise RuntimeError('Could not translate phrase: '+batch[0][:90])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--translate',action='store_true',help='Send missing English game text to Google Translate (requires explicit authorization)')
    args=parser.parse_args()
    source,phrases=collect()
    CACHE.parent.mkdir(parents=True,exist_ok=True)
    cache=json.loads(CACHE.read_text(encoding='utf-8')) if CACHE.exists() else {}
    for key,value in SEEDS.items():cache.setdefault(key,value)
    missing=sorted(phrases-cache.keys())
    if missing and not args.translate:raise SystemExit(f'{len(missing)} missing phrases. Re-run with --translate only with authorization to send game text to Google Translate.')
    chunks=[];chunk=[];length=0
    for phrase in missing:
        if chunk and (len(chunk)>=25 or length+len(phrase)>2400):chunks.append(chunk);chunk=[];length=0
        chunk.append(phrase);length+=len(phrase)
    if chunk:chunks.append(chunk)
    print(f'{len(source)} source strings; {len(phrases)} phrases; {len(missing)} missing in {len(chunks)} batches.',flush=True)
    completed=0
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for result in pool.map(request,chunks):
            cache.update(result);completed+=1
            CACHE.write_text(json.dumps(cache,ensure_ascii=False,indent=2,sort_keys=True)+'\n',encoding='utf-8')
            if completed%10==0:print(f'Translated batches {completed}/{len(chunks)}',flush=True)
    compiled={}
    for text in source:
        pieces=[]
        for segment in text.split(';'):
            stripped=segment.strip();m=re.match(r'^([^A-Za-z0-9]*)(.*)',stripped)
            prefix,clean=m.groups()
            pieces.append(prefix+cache.get(clean,clean))
        compiled[text]=';'.join(pieces)
    compiled.update(cache)
    replacements={'’':"'",'‘':"'",'“':'"','”':'"','–':'-','—':'-','…':'...','\u200b':'','\u00a0':' '}
    for key,value in compiled.items():
        for a,b in replacements.items():value=value.replace(a,b)
        if key.count('{')>value.count('{'):value+='{'*(key.count('{')-value.count('{'))
        compiled[key]=value
    target=ROOT/'assets/locales/pt-BR.json';target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(json.dumps({'language':'pt-BR','translations':compiled},ensure_ascii=False,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    print(f'Built PT-BR dictionary with {len(compiled)} translations.',flush=True)

if __name__=='__main__':main()
