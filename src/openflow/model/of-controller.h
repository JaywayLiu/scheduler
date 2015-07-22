#ifndef OF_CONTROLLER_H
#define OF_CONTROLLER_H

#include <map>
#include <list>
#include <iostream>
#include <fstream>
#include "ns3/openflow-interface.h"
#include "ns3/openflow-switch-net-device.h"

namespace ns3 {
  namespace ofi {

    class FlowInfoItem{
    public:
       FlowInfoItem(sw_flow_key* flKey, int onntwk, int availte, int avilwifi, int user);
       ~FlowInfoItem();

       void updateSize(int pktcount, ns3::Time time);
       
       sw_flow_key flowKey;
       double  dSize;
       

       bool  bOn;
       long int  nFlowId;
       int  nOnNetwork; //ID of currently used AP (0 for LTE and others for WiFi)
       int  nAvailLTEBS; //ID of connected LTE BS (always 0 in exp)
       int  nAvailWiFiAP; //ID of connected WiFi AP (starts from 1)
       
       int  nUpdateSeq; //been updated for nUpdateSeq times
       int  const window;
       int  nlstpktcount;
       ns3::Time  lasttime;
       double* pastsize;
       const int PACKETSIZE;

       double weight;
       int userIndex;
    };


    class FlowScheduler{

    public:
       FlowScheduler(std::ofstream* output):pOutStream(output){};
       void makeDecisions(std::map<int, int>* papcap, std::map<long int, FlowInfoItem*>* pallflow, std::map<int, double>* psinr, std::map<int, double>* pwifiwt); 
    private:
       std::ofstream* pOutStream;
    };

 
    class MyController : public Controller {
    public:
      MyController();
      ~MyController();
      static TypeId GetTypeId (void);
      void ReceiveFromSwitch (Ptr<OpenFlowSwitchNetDevice> swtch, ofpbuf* buffer);
     
      void doScheduling();
      void updateFlowStat();
      std::map<long int, FlowInfoItem*>* getAllFlowMap();
      void updateAFlow(FlowInfoItem* pFlowItem, int pktcount, ns3::Time time);

      void setAPCap(int apid, int cap) {mapAPCap[apid] = cap;};
      void setSrcIPWifi(uint32_t ip, int apid) {mapSrcIPWifi[ip] = apid;};
      void setSwitchAP(OpenFlowSwitchNetDevice* psw, int apid) {mapSwitchAP[psw] = apid;};
      void setAPSwitch(int apid, OpenFlowSwitchNetDevice* psw) {mapAPSwitch[apid] = psw;};
      void setSrcIPUser(uint32_t ip, int user) {mapSrcIPUser[ip] = user;}

    private:
 
      std::map<int, int> mapAPCap;
      std::map<long int, FlowInfoItem*> mapAllFlows;  //flow id = (nw_src>>8&255*199 + nw_src & 255)*100000 + tp_Src
      
      std::map<uint32_t, int> mapSrcIPWifi; //map between source IP and Wifi AP ID
      std::map<uint32_t, int> mapSrcIPUser; //map between source IP and userindex(node id);

      std::map<OpenFlowSwitchNetDevice*, int > mapSwitchAP; //map between OFSwitch and AP ID
      std::map<int, OpenFlowSwitchNetDevice* > mapAPSwitch; //map between APID and OFSwitch
      
      std::map<int, double> mapSINR;
      std::map<int, double> mapWifiWt;

      std::map<int, double> mapAPLoad;

      FlowScheduler *pmyScheduler;
      std::ofstream controllerlog; 
    };
  }
}
#endif
