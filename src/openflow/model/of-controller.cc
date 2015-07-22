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
    
    std::cout<<"src  ";
    printIPAddress(ntohl(key.flow.nw_src));
    std::cout<<" dst  ";
    printIPAddress(ntohl(key.flow.nw_dst));
    std::cout<<std::endl;
    std::cout<<"Type "<<ntohs(key.flow.dl_type)<<"  dlsrc "<<(ntohs(key.flow.dl_src[0])>>8)<<" dldst "<<(ntohs(key.flow.dl_dst[0])>>8)<<" dlvlan "<<ntohs(key.flow.dl_vlan)<<" nwproto "<<(ntohs(key.flow.nw_proto)>>8)<<" tpsrc "<<ntohs(key.flow.tp_src)<<" tpdst "<<ntohs(key.flow.tp_dst)<<std::endl;
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
  }
  return;
}


MyController::MyController(){
     controllerlog.open("mycontroller.log");
     pmyScheduler = new FlowScheduler(&controllerlog); 
}

MyController::~MyController(){
     delete pmyScheduler;
     std::map<int, FlowInfoItem*>::iterator mit = mapAllFlows.begin();
     while(mit != mapAllFlows.end()){
        delete mit->second;
        ++mit;
     }
}



void MyController::updateFlowStat(){
    //first set all flow off
    std::map<int, FlowInfoItem*>::iterator mit = mapAllFlows.begin();
    while(mit != mapAllFlows.end()){
       mit->second->bOn = false;
      ++mit;
    }
    //read switch to update
    //*****HELP****//
    //m_switches


    //Then remove dead flows
    while(mit != mapAllFlows.end()){
       if(mit->second->bOn == false){
           delete mit->second;
           mapAllFlows.erase(mit++);
       }
       else
           ++mit;
    }
}
 
std::map<int, FlowInfoItem*>* MyController::getAllFlowMap(){
    return &mapAllFlows;
}


void MyController::updateAFlow(sw_flow_key* key, int byte, int time){
    int nflid = (key->flow.nw_src&255)*1000000 + key->flow.tp_src;
    //std::map<int, FlowInfoItem*>* getAllFlowMap();
    std::map<int, FlowInfoItem*>::iterator fmit = mapAllFlows.find(nflid);
    if(fmit != mapAllFlows.end()){//find one
       fmit->second->updateSize(byte, time);
    }
    else{//create one
       int onnet=0, avlte=0, avwifi=1;
       //****HELP******//get these values

       FlowInfoItem *pNewFlow = new FlowInfoItem(key, onnet, avlte, avwifi);
       pNewFlow->updateSize(byte, time);
       mapAllFlows[nflid] = pNewFlow;
    }
}



void MyController::doScheduling(){
    updateFlowStat();
    pmyScheduler->makeDecisions(pmapAPCap, &mapAllFlows);
}

///////////////////////////////////////////FlowScheduler/////////////////////////////////////////////////

void FlowScheduler::makeDecisions(std::map<int, int>* papcap, std::map<int, FlowInfoItem*>* pallflow){
} 

///////////////////////////////////////////FlowScheduler/////////////////////////////////////////////////

FlowInfoItem::FlowInfoItem(sw_flow_key* key, int onntwk, int avlte, int avwifi):window(5){
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
    nFlowId = (key->flow.nw_src&255)*1000000 + key->flow.tp_src;

    nOnNetwork = onntwk; //ID of currently used AP (0 for LTE and others for WiFi)
    nAvailLTEBS = avlte; //ID of connected LTE BS (always 0 in exp)
    nAvailWiFiAP = avwifi; //ID of connected WiFi AP (starts from 1)
    nUpdateSeq = 0; //been updated for nUpdateSeq times

    nlstbyte = 0;
    nlsttime = 0;
    pastsize =  new double[window];
    for(int i=0; i<window; i++)
          pastsize[i] = 0;
}

FlowInfoItem::~FlowInfoItem(){
    delete pastsize;
}

void FlowInfoItem::updateSize(int byte, int time){
    if(nUpdateSeq == 0){
       dSize = 0;
    }
    else if(nUpdateSeq<=window){
       pastsize[nUpdateSeq-1] = (double)(byte - nlstbyte)/(time - nlsttime);
       dSize = 0;
       for(int i=0; i<nUpdateSeq; i++)
           dSize += pastsize[i];
       dSize /= nUpdateSeq;
    }
    else{
       pastsize[(nUpdateSeq-1)%window] = (double)(byte - nlstbyte)/(time - nlsttime);
       dSize = 0;
       for(int i=0; i<window; i++)
           dSize += pastsize[i];
       dSize /= window;
    }

    nlstbyte = byte;
    nlsttime = time;
    nUpdateSeq++;
    bOn = true;//still live
}

}
}

