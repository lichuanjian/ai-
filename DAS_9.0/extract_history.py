import json
p=r"C:\Users\admin\.codex\sessions\2026\08\16\rollout-2026-08-16T00-09-49-01a0062f-c553-7533-839c-742c338dc551.jsonl"
out=[]
for n,line in enumerate(open(p,encoding='utf-8')):
    try: x=json.loads(line)
    except: continue
    typ=x.get('type'); pay=x.get('payload',{})
    role=pay.get('role')
    if typ=='response_item' and role in ('user','assistant'):
      cont=pay.get('content',[])
      texts=[]
      for c in cont:
        if isinstance(c,dict) and c.get('type') in ('input_text','output_text'):
          texts.append(c.get('text',''))
      if texts: out.append((n,role,'\n'.join(texts)))
    elif typ=='message' and pay.get('role') in ('user','assistant'):
      out.append((n,pay.get('role'),str(pay.get('content'))))
with open('history_messages.txt','w',encoding='utf-8') as f:
  for n,r,t in out:
    f.write(f'\n===== LINE {n} {r} =====\n{t}\n')
print('messages',len(out))
for n,r,t in out:
 print(f'{n} {r} chars={len(t)}')
