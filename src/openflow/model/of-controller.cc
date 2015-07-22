#define __STDC_FORMAT_MACROS
#include "of-controller.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/ipv4-list-routing.h"


#include <iostream>
#include <string>
namespace ns3 {
namespace ofi {
NS_LOG_COMPONENT_DEFINE("MyController");


//////////////////////////////////////////////////////controller code///////////////////////////////////////////////

void printIPAddress(uint32_t address){
  uint32_t unit;
  for(int x=3;x>=0;x--){
	unit = (address>>(x*8)) & (255);
        std::cout<<unit<<" ";
  }
}

TypeId MyController::GetTypeId (void){
  static TypeId tid = TypeId ("ns3::ofi::MyController")
    .SetParent<Controller> ()
    .SetGroupName ("OpenFlow")
    .AddConstructor<MyController> ();
  return tid;
}

void MyController::ReceiveFromSwitch (Ptr<OpenFlowSwitchNetDevice> swtch, ofpbuf* buffer) {
  if (m_switches.find(swtch) == m_switches.end()) {
    NS_LOG_ERROR("Can't send to this switch, not registered to the Controller.");
    return;
  }

  //Received any packet at this point, so we pull the header to figure out what type of packet we are handing
  uint8_t type = GetPacketType(buffer);
  if (type == OFPT_PACKET_IN) {
    ofp_packet_in * opi = (ofp_packet_in*)ofpbuf_try_pull (buffer, offsetof (ofp_packet_in, data));
    int port = ntohs (opi->in_port);
    
    //Matching key
    sw_flow_key key;
    key.wildcards = 0;
    flow_extract (buffer, port != -1? port: OFPP_NONE, &key.flow);
    uint16_t out_port = OFPP_FLOOD;
    uint16_t in_port = ntohs (key.flow.in_port);
    
    std::cout<<"@"<<Simulator::Now()<<" SW "<<mapSwitchAP[GetPointer(swtch)]<<"("<<GetPointer(swtch)<<") Type "<<ntohs(key.flow.dl_type)<<" nwproto "<<(ntohs(key.flow.nw_proto)>>8)<<" tpsrc "<<ntohs(key.flow.tp_src)<<" tpdst "<<ntohs(key.flow.tp_dst);
    std::cout<<" src  ";
    printIPAddress(ntohl(key.flow.nw_src));
    std::cout<<" dst  ";
    printIPAddress(ntohl(key.flow.nw_dst));
    std::cout<<std::endl;
    //<<"  dlsrc "<<(ntohs(key.flow.dl_src[0])>>8)<<" dldst "<<(ntohs(key.flow.dl_dst[0])>>8)<<" dlvlan "<<ntohs(key.flow.dl_vlan)
    //*/

    if(in_port==1)
       out_port=0;
    else
       out_port=1;
       
    ofp_action_output x[1];
    x[0].type = htons (OFPAT_OUTPUT);
    x[0].len = htons (sizeof(ofp_action_output));
    x[0].port = out_port;
    NS_LOG_FUNCTION (out_port);
    ofp_flow_mod* ofm;
    if(ntohs(key.flow.dl_type) == 0x806)
        ofm = BuildFlow(key, opi->buffer_id, OFPFC_ADD, x, sizeof(x), OFP_FLOW_PERMANENT, OFP_FLOW_PERMANENT);
    else
        ofm = BuildFlow(key, opi->buffer_id, OFPFC_ADD, x, sizeof(x), 2, OFP_FLOW_PERMANENT);

    SendToSwitch (swtch, ofm, ofm->header.length);

    if(ntohs(key.flow.dl_type) == 0x800){
       long int nflid = (((ntohl(key.flow.nw_src)>>8)&255)*100 + (ntohl(key.flow.nw_src)&255))*100000 + (ntohs(key.flow.tp_src));
       int onnet=mapSwitchAP[GetPointer(swtch)], avlte=0, avwifi=mapSrcIPWifi[ntohl(key.flow.nw_src)];  

   
       FlowInfoItem *pFlowItem = new FlowInfoItem(&key, onnet, avlte, avwifi, mapSrcIPUser[ntohl(key.flow.nw_src)]);
       mapAllFlows[nflid] = pFlowItem;

       std::cout<<"Create flow "<<pFlowItem->nFlowId<<" ";
       printIPAddress(ntohl(key.flow.nw_src));
       std::cout<<(ntohs(key.flow.tp_src))<<" onnet "<<pFlowItem->nOnNetwork <<" avlte "<<pFlowItem->nAvailLTEBS<<" avwifi "<< pFlowItem->nAvailWiFiAP
             <<" size "<<pFlowItem->dSize<<" user "<<pFlowItem->userIndex<<std::endl;
    }
  }
  return;
}


MyController::MyController(){
     controllerlog.open("mycontroller.log");
     pmyScheduler = new FlowScheduler(&controllerlog); 
}

MyController::~MyController(){
     delete pmyScheduler;
     std::map<long int, FlowInfoItem*>::iterator mit = mapAllFlows.begin();
     while(mit != mapAllFlows.end()){
        delete mit->second;
        ++mit;
     }
}



void MyController::updateFlowStat(){
    //delete expired flows on switches, when all flows are removed on a switch, chain_timeout may not be called due to no inpacket
    sw_chain* pswchain;
    std::map<int, OpenFlowSwitchNetDevice* >::iterator swit = mapAPSwitch.begin();
    while(swit != mapAPSwitch.end()){
        pswchain = swit->second->GetChain();
        List deleted = LIST_INITIALIZER (&deleted);
        sw_flow *f, *n;
        chain_timeout (pswchain, &deleted);
        LIST_FOR_EACH_SAFE (f, n, sw_flow, node, &deleted)
        {
           std::ostringstream str;
           str << "Flow [";
           for (int i = 0; i < 6; i++)
              str << (i!=0 ? ":" : "") << std::hex << f->key.flow.dl_src[i]/16 << f->key.flow.dl_src[i]%16;
           str << " -> ";
           for (int i = 0; i < 6; i++)
              str << (i!=0 ? ":" : "") << std::hex << f->key.flow.dl_dst[i]/16 << f->key.flow.dl_dst[i]%16;
           str <<  "] expired.";
	
           NS_LOG_INFO (str.str ());
           //SendFlowExpired (f, (ofp_flow_expired_reason)f->reason);
           list_remove (&f->node);
           flow_free (f);
        }
        ++swit;
    }
    //then check flow
    mapAPLoad.clear();
    std::map<int, double>::iterator ldit;
    std::map<long int, FlowInfoItem*>::iterator mit = mapAllFlows.begin();
    while(mit != mapAllFlows.end()){
       std::cout<<"Check flow "<<mit->first<<" "<<mit->second->nFlowId<<std::endl;
       pswchain = mapAPSwitch[mit->second->nOnNetwork]->GetChain ();
       sw_flow* flow = chain_lookup(pswchain, &mit->second->flowKey);
       if(flow){
          updateAFlow(mit->second, flow->packet_count, ns3::Simulator::Now());
          ldit = mapAPLoad.find(mit->second->nOnNetwork);
          if(ldit != mapAPLoad.end())
             ldit->second += mit->second->dSize;
          else
             mapAPLoad[mit->second->nOnNetwork] = mit->second->dSize;
          ++mit; 
       }
       else{
          std::cout<<"Erase"<<std::endl;
          mit->second->bOn=false;
          delete mit->second;
          mapAllFlows.erase(mit++);
       }
    }
    double dTotalLoad = 0;
    std::cout<<"Load Stat"<<std::endl;
    ldit = mapAPLoad.begin();
    while(ldit != mapAPLoad.end()){
      dTotalLoad += ldit->second;
      std::cout<<"AP "<<ldit->first<<" Load "<<ldit->second<<std::endl;
      ++ldit;
    }
    std::cout<<"Total Load "<<dTotalLoad<<std::endl;
}
 
std::map<long int, FlowInfoItem*>* MyController::getAllFlowMap(){
    return &mapAllFlows;
}


void MyController::updateAFlow(FlowInfoItem* pFlowItem, int pktcount, ns3::Time time){  
    pFlowItem->updateSize(pktcount, time);

    std::cout<<"@"<<time<<" Update flow "<<pFlowItem->nFlowId<<" ";
    printIPAddress(ntohl(pFlowItem->flowKey.flow.nw_src));
    std::cout<<(ntohs(pFlowItem->flowKey.flow.tp_src))<<" onnet "<<pFlowItem->nOnNetwork <<" avlte "<<pFlowItem->nAvailLTEBS<<" avwifi "<< pFlowItem->nAvailWiFiAP<<" size "<<pFlowItem->dSize<<" pktcount "<<pktcount<<std::endl;
    
}



void MyController::doScheduling(){
    std::cout<<std::endl<<"@ "<<Simulator::Now()<<" Call Scheduling" <<std::endl;
    updateFlowStat();
    pmyScheduler->makeDecisions(&mapAPCap, &mapAllFlows, &mapSINR, &mapWifiWt);
    //std::cout<<"FlowMapSize "<<mapAllFlows.size()<<std::endl;
}

///////////////////////////////////////////FlowScheduler/////////////////////////////////////////////////

void FlowScheduler::makeDecisions(std::map<int, int>* papcap, std::map<long int, FlowInfoItem*>* pallflow, std::map<int, double>* psinr, std::map<int, double>*){

    std::map<long int, FlowInfoItem*>::iterator flit =  pallflow->begin();
    while(flit != pallflow->end()){
       std::cout<<"Schedule flow "<<flit->second->nFlowId<<" ";
       printIPAddress(ntohl(flit->second->flowKey.flow.nw_src));
       std::cout<<(ntohs(flit->second->flowKey.flow.tp_src))<<" onnet "<<flit->second->nOnNetwork <<" avlte "<<flit->second->nAvailLTEBS<<" avwifi "<< flit->second->nAvailWiFiAP<<" size "<<flit->second->dSize<<" to ";
    
       if(flit->second->nOnNetwork == 0){
           flit->second->nOnNetwork = flit->second->nAvailWiFiAP;
           std::cout<<"WiFi "<<flit->second->nAvailWiFiAP<<std::endl;     
       }
       else{
           flit->second->nOnNetwork = flit->second->nAvailLTEBS;
           std::cout<<"LTE "<<flit->second->nAvailLTEBS<<std::endl;
       }
       ++flit;
   }
} 

///////////////////////////////////////////FlowScheduler/////////////////////////////////////////////////

FlowInfoItem::FlowInfoItem(sw_flow_key* key, int onntwk, int avlte, int avwifi, int user):window(5), PACKETSIZE(1048){
    flowKey.wildcards = key->wildcards;                                 // Wildcard fields
    flowKey.flow.in_port = key->flow.in_port;                                // Input switch port
    memcpy (flowKey.flow.dl_src, key->flow.dl_src, sizeof flowKey.flow.dl_src); // Ethernet source address.
    memcpy (flowKey.flow.dl_dst, key->flow.dl_dst, sizeof flowKey.flow.dl_dst); // Ethernet destination address.
    flowKey.flow.dl_vlan = key->flow.dl_vlan;                                // Input VLAN OFP_VLAN_NONE;
    flowKey.flow.dl_type = key->flow.dl_type;                                // Ethernet frame type ETH_TYPE_IP;
    flowKey.flow.nw_proto = key->flow.nw_proto;                              // IP Protocol
    flowKey.flow.nw_src = key->flow.nw_src;                                  // IP source address
    flowKey.flow.nw_dst = key->flow.nw_dst;                                  // IP destination address
    flowKey.flow.tp_src = key->flow.tp_src;                                  // TCP/UDP source port
    flowKey.flow.tp_dst = key->flow.tp_dst;                                  // TCP/UDP destination port
    flowKey.flow.mpls_label1 = key->flow.mpls_label1;                        // Top of label stack htonl(MPLS_INVALID_LABEL);
    flowKey.flow.mpls_label2 = key->flow.mpls_label1;                        // Second label (if available) htonl(MPLS_INVALID_LABE

  
    dSize = 0;
    bOn = true;
    //std::cout<<((ntohl(key->flow.nw_src)>>8)&255)<<std::endl;
    nFlowId = (((ntohl(key->flow.nw_src)>>8)&255)*100 + (ntohl(key->flow.nw_src)&255))*100000 + (ntohs(key->flow.tp_src));

    nOnNetwork = onntwk; //ID of currently used AP (0 for LTE and others for WiFi)
    nAvailLTEBS = avlte; //ID of connected LTE BS (always 0 in exp)
    nAvailWiFiAP = avwifi; //ID of connected WiFi AP (starts from 1)
    nUpdateSeq = 0; //been updated for nUpdateSeq times

    nlstpktcount = 0;
    lasttime = ns3::Now();
    pastsize =  new double[window];
    for(int i=0; i<window; i++)
          pastsize[i] = 0;

    weight = 1;
    userIndex = user;
}

FlowInfoItem::~FlowInfoItem(){
    delete pastsize;
}

void FlowInfoItem::updateSize(int npktcount, ns3::Time time){
    nUpdateSeq++; //the nUpdateSeq-th update
    //std::cout<<"Update seq "<<nUpdateSeq<<" lastcount "<<nlstpktcount<<" lasttime "<<lasttime<<" newcount "<<npktcount<<" newtime "<<time<<std::endl;
    if((nUpdateSeq)<=window){
       pastsize[nUpdateSeq-1] = PACKETSIZE*1000*((double)npktcount - nlstpktcount)/(time.GetMilliSeconds() - lasttime.GetMilliSeconds());
       dSize = 0;
       for(int i=0; i < nUpdateSeq; i++)
           dSize += pastsize[i];
       dSize /= (nUpdateSeq);
    }
    else{
       pastsize[(nUpdateSeq-1)%window] = PACKETSIZE*1000*((double)npktcount - nlstpktcount)/(time.GetMilliSeconds() - lasttime.GetMilliSeconds());
       dSize = 0;
       for(int i=0; i<window; i++)
           dSize += pastsize[i];
       dSize /= window;
    }

    nlstpktcount = npktcount;
    lasttime = time;
    
    bOn = true;//still live
}

}
}

