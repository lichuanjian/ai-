from pypdf import PdfReader
p=r'D:\daimaxuexi\_caj_work\分布式光纤振动传感系统模式识别方法研究_彭宽.pdf'
r=PdfReader(p)
print('pages',len(r.pages))
with open('peng_pdf_text.txt','w',encoding='utf-8') as f:
 for i,page in enumerate(r.pages):
  f.write(f'\n===== PDF PAGE {i+1} =====\n')
  f.write(page.extract_text() or '')
