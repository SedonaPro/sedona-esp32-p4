"""Check that extending an exported application preserves existing content."""
import sys
import xml.etree.ElementTree as ET
def components(root):
    out={}
    def walk(node,path):
        for c in node.findall('comp'):
            key=path+'/'+c.get('name');out[key]=c;walk(c,key)
    walk(root.find('app'),'')
    return out
before=ET.parse(sys.argv[1]).getroot();after=ET.parse(sys.argv[2]).getroot()
a,b=components(before),components(after)
for path,old in a.items():
    assert path in b, 'missing component '+path
    new=b[path]
    assert old.get('id')==new.get('id') and old.get('type')==new.get('type'),path
    props={x.get('name'):x.get('val') for x in new.findall('prop')}
    assert all(props.get(x.get('name'))==x.get('val') for x in old.findall('prop')),path
oldlinks={(x.get('from'),x.get('to')) for x in before.findall('links/link')}
newlinks={(x.get('from'),x.get('to')) for x in after.findall('links/link')}
assert oldlinks<=newlinks
ids=[x.get('id') for x in b.values()];assert len(ids)==len(set(ids))
for name,count in [('ai',4),('ao',2),('di',6),('do',4)]:
    for i in range(1,count+1): assert '/io/'+name+str(i) in b
assert '/io/counter' in b
for service in ['diag','spi','i2c','serial','mqtt','rtu','ota']:assert '/service/'+service in b
print('App migration PASS: %d existing components, %d existing links preserved; %d total components' % (len(a),len(oldlinks),len(b)))
