#ifndef OF_CONTROLLER_H
#define OF_CONTROLLER_H

#include <map>
#include <list>
#include <iostream>
#include <fstream>
#include "ns3/openflow-interface.h"
#include "ns3/openflow-switch-net-device.h"

using std::vector;
using std::map;
using std::set;

namespace ns3 {
  namespace ofi {

    struct OrgFlow{
       long int nId;
       double   dStart;
       double   dLen;
       int      nSize; //bps
    };

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
       double utility;
    };


    class FlowScheduler{

    public:
       FlowScheduler(std::ofstream* output):pOutStream(output){
        ulogFp = fopen("utility0.log", "w");
        ulogFpR = fopen("utility1.log", "w");
     randomp = CreateObject<UniformRandomVariable> ();
     approax = new float[10];
     approax[0]= 0;
     approax[1] =2500000; //Bps
     approax[2] =1800000;
     approax[3] =1250000;
     approax[4] =947500;
     approax[5] =687500;
     //approax[5] =10;
       
       };
       ~FlowScheduler()
       {
           fclose(ulogFp);
           fclose(ulogFpR);
           delete[] approax;
       }
       void makeDecisions(std::map<int, int>* papcap, std::map<long int, FlowInfoItem*>* pallflow, std::map<int, double>* psinr, std::map<int, double>* pwifiwt);

       //the dry run option enable user to call it without changing the real scheduling, only print the utility value
       void makeDecisionsRandom(std::map<int, int>* papcap, std::map<long int, FlowInfoItem*>* pallflow, std::map<int, double>* psinr, std::map<int, double>* pwifiwt, int dryrun); 

       void makeDecisionsEven(std::map<int, int>* papcap, std::map<long int, FlowInfoItem*>* pallflow, std::map<int, double>* psinr, std::map<int, double>* pwifiwt); 
    private:

       //??do we need this
       std::ofstream* pOutStream;

            void divideByCoverage();
            double sumAllLTEWeights();
            double findMaxConfig(int apIndex, unsigned int* result, int* nflowRe, double* lteSumP);
            double calcUtility(int apIndex, vector<long int>*vv, int* re, int nflow, double* lteSum, bool isWiFiOnly);

            void setUtility(vector<long int>* vp, map<int, double>* up);

            //this is for updating the lte flow utility once more flows are assigned to lte
            void updateOldUtility(double lteSumF);

            double sumOldLteUtility(double lteSum);


            //for the utility of random decision
            double calcLteU(vector<long int>* lid);

            //first int is AP index, the vector  is the set of flows under that AP
            std::map<int, vector<long int> > apToFlowSet;

	   //the map from ap index to the set of users under it
            std::map<int, set<int> > apToUserSet;

            //from user index to list of flow indices
            std::map<int, vector<long int> > userFlowList;

            //the AP indices that are assigned 
	    std::vector<int> assigned;

        //this records all the flows assigned to lte up to now
        std::vector<long int> lteFlowIDs;

            //the first int is the user index 
            //second double LTE weight of the flow 
            std::map<int, double> lteFlows;

            std::map<int, double>* wifiW;
            std::map<int, double>* lteW;

            //these two are for perserving the utility values
            std::map<int, double> uMax;
            std::map<int,double> uCalc;


            std::map<int, int>* capMap;
            std::map<long int, FlowInfoItem*>* pallflow;
            FILE* ulogFp;
            FILE* ulogFpR;
             Ptr<UniformRandomVariable> randomp;
             float* approax;
    
    };

 
    class MyController : public Controller {
    public:
      MyController();
      ~MyController();
      static TypeId GetTypeId (void);
      void ReceiveFromSwitch (Ptr<OpenFlowSwitchNetDevice> swtch, ofpbuf* buffer);
     
      void doScheduling();

      void updateFlowStat(bool isPrint);

      std::map<long int, FlowInfoItem*>* getAllFlowMap();
      void updateAFlow(FlowInfoItem* pFlowItem, int pktcount, ns3::Time time);

      void setAPCap(int apid, int cap) {mapAPCap[apid] = cap;}
      void setSType(uint16_t tt);
      void setSrcIPWifi(uint32_t ip, int apid) {mapSrcIPWifi[ip] = apid;}
      void setSwitchAP(OpenFlowSwitchNetDevice* psw, int apid) {mapSwitchAP[psw] = apid;}
      void setAPSwitch(int apid, OpenFlowSwitchNetDevice* psw) {mapAPSwitch[apid] = psw;}
      void setSrcIPUser(uint32_t ip, int user) {mapSrcIPUser[ip] = user;}

      void setUENumber(uint32_t n) {maxUENumber = n;}

      void setDefaultSINR();

      void setOrgFlow(long int id, double start, double len, int size);
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

      uint32_t maxUENumber;

      std::vector<OrgFlow> vecOrgFlow;

      FlowScheduler *pmyScheduler;
      std::ofstream controllerlog;

      uint16_t stype; //scheduler type

            FILE* realUFp;
            FILE* throughp;
    };
  }
}
#endif
