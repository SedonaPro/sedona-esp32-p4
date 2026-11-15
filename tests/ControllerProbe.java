import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import sedona.*;
import sedona.dasp.DaspSocket;
import sedona.sox.*;
/** Read-only checks by default; mqtt/ota use a temporary explicit bench endpoint. */
public class ControllerProbe {
  static SoxClient client;
  static SoxComponent path(SoxComponent root,String name)throws Exception {
    SoxComponent p=root;
    for(String part:name.split("/"))if(!part.isEmpty()){p=p.child(part);if(p==null)throw new Exception("missing "+name);p=client.load(p.id());}
    return p;
  }
  static Value get(SoxComponent p,String slot)throws Exception{return client.readProp(p,p.type.slot(slot));}
  static void set(SoxComponent p,String slot,Value value)throws Exception{client.write(p,p.type.slot(slot),value);}
  static void text(SoxComponent p,String slot,String text)throws Exception{
    set(p,slot,p.type.slot(slot).isAsStr()?Str.make(text):new Buf(text.getBytes(StandardCharsets.UTF_8)));
  }
  static void action(SoxComponent p,String slot)throws Exception{client.invoke(p,p.type.slot(slot),null);}
  static String payload(SoxComponent p)throws Exception{return new String(((Buf)get(p,"receivedPayload")).trim(),StandardCharsets.UTF_8);}
  public static void main(String[] args)throws Exception {
    DaspSocket socket=DaspSocket.open(-1,null,DaspSocket.SESSION_QUEUING);
    client=new SoxClient(socket,InetAddress.getByName("127.0.0.1"),1876,"admin",System.getenv("SEDONA_ADMIN_PASSWORD"));
    try {
      client.connect();SoxComponent root=client.loadApp();String cmd=args[0];
      if(cmd.equals("status")){
        SoxComponent d=path(root,"service/diag");
        System.out.println("diag.componentId="+d.id());
        for(String slot:new String[]{"cpu0","cpu1","freeHeap","minFreeHeap","largestFreeBlock","vmStackFree","cycles","overruns","periodAvgMs","periodMaxMs","fault"})System.out.println("diag."+slot+"="+get(d,slot));
        int count=0;
        for(String prefix:new String[]{"ai","ao","di","do"})for(int i=1;i<=(prefix.equals("ai")?4:prefix.equals("ao")?2:prefix.equals("di")?6:4);i++){
          SoxComponent p=path(root,"io/"+prefix+i);
          if(!get(p,"enabled").toString().equals("false") || !get(p,"pin").toString().equals("-1"))throw new Exception("unexpected enabled IO "+p.name());
          count++;
        }
        path(root,"io/counter");
        for(String service:new String[]{"i2c","spi","serial","mqtt","rtu","ota"})path(root,"service/"+service);
        System.out.println("IO defaults checked: "+count+" points + counter; all services present");
      } else if(cmd.equals("mqtt")){
        SoxComponent m=path(root,"service/mqtt");Map<String,Value> saved=new LinkedHashMap<>();
        for(String k:new String[]{"enabled","broker","clientId","subscribeTopic","publishTopic","publishPayload","qos","retain"})saved.put(k,get(m,k));
        try {
          set(m,"enabled",Bool.FALSE);Thread.sleep(400);
          text(m,"broker","mqtt://127.0.0.1:18830");text(m,"clientId","sedona-p4-bench");text(m,"subscribeTopic","bench/#");
          set(m,"qos",Int.make(1));set(m,"enabled",Bool.TRUE);
          boolean ok=false;
          for(int i=0;i<75;i++){Thread.sleep(200);if(get(m,"connected").toString().equals("true") && payload(m).equals("from-broker")){ok=true;break;}}
          if(!ok)throw new Exception("MQTT connect/subscribe failed; fault="+get(m,"fault"));
          text(m,"publishTopic","bench/tx");text(m,"publishPayload","from-esp32");action(m,"publish");ok=false;
          for(int i=0;i<50;i++){Thread.sleep(200);if(payload(m).equals("from-esp32")){ok=true;break;}}
          if(!ok)throw new Exception("MQTT publish/echo failed; fault="+get(m,"fault"));
          System.out.println("MQTT connect, subscribe, receive, QoS1 publish and broker echo PASS; received="+get(m,"received")+" dropped="+get(m,"dropped"));
        } finally {
          set(m,"enabled",Bool.FALSE);Thread.sleep(400);
          for(Map.Entry<String,Value> e:saved.entrySet())if(!e.getKey().equals("enabled"))set(m,e.getKey(),e.getValue());
          set(m,"enabled",saved.get("enabled"));
        }
      } else if(cmd.equals("ota")){
        SoxComponent o=path(root,"service/ota");
        set(o,"utcSeconds",sedona.Long.make(java.time.Instant.now().getEpochSecond()));action(o,"setClock");
        text(o,"caCert",Files.readString(Paths.get(args[1])));text(o,"url","https://127.0.0.1:18443/firmware.bin");set(o,"enabled",Bool.TRUE);
        action(o,"update");System.out.println("OTA requested: phase="+get(o,"phase")+" fault="+get(o,"fault"));
      } else throw new Exception("unknown command");
      System.out.println("CONTROLLER "+cmd+" OK");
    } finally {client.close();socket.close();}
  }
}
