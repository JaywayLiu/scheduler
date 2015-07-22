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
       FlowInfoItem(sw_flow_key* flKey, int onntwk, int availte, int avilwifi);
       ~FlowInfoItem();

       void updateSize(int byte, int time);
       
       sw_flow_key flowKey;
       double  dSize;

       bool  bOn;
       int  nFlowId;
       int  nOnNetwork; //ID of currently used AP (0 for LTE and others for WiFi)
       int  nAvailLTEBS; //ID of connected LTE BS (always 0 in exp)
       int  nAvailWiFiAP; //ID of connected WiFi AP (starts from 1)
       
       int  nUpdateSeq; //been updated for nUpdateSeq times
       int  const window;
       int  nlstbyte;
       int  nlsttime;
       double* pastsize;
    };


    class FlowScheduler{

    public:
       FlowScheduler(std::ofstream* output):pOutStream(output){};
       void makeDecisions(std::map<int, int>* papcap, std::map<int, FlowInfoItem*>* pallflow); 
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
      void setAPCap(std::map<int, int>* papcap) {pmapAPCap = papcap;};
      void updateFlowStat();
      std::map<int, FlowInfoItem*>* getAllFlowMap();
      void updateAFlow(sw_flow_key* key, int byte, int time);

    private:
      std::map<int, int>* pmapAPCap;
      std::map<int, FlowInfoItem*> mapAllFlows;  //flow id = (nw_src & 255)*100000 + tp_Src
      FlowScheduler *pmyScheduler;
      std::ofstream controllerlog; 
    };
  }
}
#endif
