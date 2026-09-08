import json
p=r"C:\Users\admin\.codex\sessions\2026\08\16\rollout-2026-08-16T00-09-49-01a0062f-c553-7533-839c-742c338dc551.jsonl"
for n,line in enumerate(open(p,encoding='utf-8')):
 if n in range(11500,12300):
  try:x=json.loads(line)
  except:continue
  pay=x.get('payload',{})
  if x.get('type')=='response_item' and pay.get('role') in ('user','assistant'):
   texts=[]
   for c in pay.get('content',[]):
    if isinstance(c,dict) and c.get('type') in ('input_text','output_text'): texts.append(c.get('text',''))
   if texts:
    print('\n###',n,pay.get('role')); print(''.join(texts)[:5000])
