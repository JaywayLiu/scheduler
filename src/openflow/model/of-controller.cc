#define __STDC_FORMAT_MACROS
#include "of-controller.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/double.h"


#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <cassert>
using std::cerr;
using std::cout;
using std::endl;

using std::ostream;
ostream cnull(NULL);

#define cerr cnull

#include <iostream>
#include <string>
namespace ns3 {
namespace ofi {
NS_LOG_COMPONENT_DEFINE("MyController");


typedef map<long int, FlowInfoItem*> fmap;
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

void MyController::setSType(uint16_t tt){
   stype = tt;
   if(stype == 0){
        realUFp = fopen("realU0.log", "w");
        throughp = fopen("through0.log", "w");
     }
     else if(stype == 1){
        realUFp = fopen("realU1.log", "w");
        throughp = fopen("through1.log", "w");
     }
     else{
        realUFp = fopen("realU2.log", "w");
        throughp = fopen("through2.log", "w");
     }
}

MyController::MyController() :stype(0){
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
    fclose(realUFp);
    fclose(throughp);
}



void MyController::updateFlowStat(bool isPrint){
    //delete expired flows on switches, when all flows are removed on a switch, chain_timeout may not be called due to no inpacket
    sw_chain* pswchain;
    std::map<int, OpenFlowSwitchNetDevice* >::iterator swit = mapAPSwitch.begin();
    double realU = 0;
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
          realU+= (mit->second->weight) * log(mit->second->dSize);
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
      std::cout<<"AP "<<ldit->first<<" Load "<<ldit->second*8<<" bps "<<std::endl;
      ++ldit;
    }
    std::cout<<"Total Load "<<dTotalLoad*8<<" bps "<<std::endl;  
   
    double dSrcLoad = 0;
    double dNow = Simulator::Now().GetSeconds();
    for(uint16_t i=0; i<vecOrgFlow.size();i++){
        if(dNow >= vecOrgFlow[i].dStart && dNow <= vecOrgFlow[i].dStart + vecOrgFlow[i].dLen)
            dSrcLoad += vecOrgFlow[i].nSize;
    }
    std::cout<<"Total Src Load "<<dSrcLoad<<" bps "<<std::endl;
   
    cout<<"Real Utility: "<<realU<<endl;

    if(isPrint)
    {
       fprintf(realUFp, "@%.4f with real U %.4f\n", dNow, realU);
       fprintf(throughp, "@%.4f  src throuput %.4f bps real throuput %.4f bps \n", dNow, dSrcLoad, dTotalLoad*8);
    }
    
}
 
std::map<long int, FlowInfoItem*>* MyController::getAllFlowMap(){
    return &mapAllFlows;
}


void MyController::setOrgFlow(long int id, double start, double len, int size){
    OrgFlow aFlow; 
    aFlow.nId = id; 
    aFlow.dStart = start; 
    aFlow.dLen = len; 
    aFlow.nSize = size; 
    vecOrgFlow.push_back(aFlow);
}


void MyController::setDefaultSINR()
{
    for(uint32_t i=0; i< maxUENumber; i++)
    {
        mapSINR[i] = 1;
        mapWifiWt[i] = 1;
    }
}


void MyController::updateAFlow(FlowInfoItem* pFlowItem, int pktcount, ns3::Time time){  
    pFlowItem->updateSize(pktcount, time);

    std::cout<<"@"<<time<<" Update flow "<<pFlowItem->nFlowId<<" ";
    printIPAddress(ntohl(pFlowItem->flowKey.flow.nw_src));
    std::cout<<(ntohs(pFlowItem->flowKey.flow.tp_src))<<" onnet "<<pFlowItem->nOnNetwork <<" avlte "<<pFlowItem->nAvailLTEBS<<" avwifi "<< pFlowItem->nAvailWiFiAP<<" size "<<pFlowItem->dSize<<" pktcount "<<pktcount<<std::endl;
    
}



void MyController::doScheduling(){
    std::cout<<std::endl<<"@ "<<Simulator::Now()<<" Call Scheduling" <<std::endl;
//<<<<<<< HEAD
    //updateFlowStat(true);
    //pmyScheduler->makeDecisions(&mapAPCap, &mapAllFlows, &mapSINR, &mapWifiWt);
    
//=======
    updateFlowStat(false);
    if(stype ==0)
    {
    pmyScheduler->makeDecisions(&mapAPCap, &mapAllFlows, &mapSINR, &mapWifiWt);
    }
    else if(stype ==1)
    {
    pmyScheduler->makeDecisionsRandom(&mapAPCap, &mapAllFlows, &mapSINR, &mapWifiWt, 0);
    }
    else if(stype == 2)
    {
    pmyScheduler->makeDecisionsEven(&mapAPCap, &mapAllFlows, &mapSINR, &mapWifiWt);
    }
    else
    {
        cout<<"arg error in doScheduling"<<endl;
        exit(0);
    }

//>>>>>>> bfc9bb7c228cd15613e578516bfebcdc4bb7419f
    //std::cout<<"FlowMapSize "<<mapAllFlows.size()<<std::endl;
}

///////////////////////////////////////////FlowScheduler/////////////////////////////////////////////////
//
//




	/////////////////////////////////////////////////////////////////////////////////////ljw

        void
        FlowScheduler::divideByCoverage() {

            std::pair < std::map<int, std::vector<long int> >::iterator, bool> ret;
            std::pair < std::map<int, std::set<int> >::iterator, bool> retU;
            std::pair < std::map<int, std::vector<long int> >::iterator, bool> retUserFlow;

            apToFlowSet.clear();
            apToUserSet.clear();
            userFlowList.clear();

            int iii = 0;

            for (std::map<long int, FlowInfoItem*>::iterator it = pallflow->begin(); it != pallflow->end(); it++, iii++) {
                cerr<<iii<<" "<< it->first << " " << it->second->nAvailWiFiAP<<" "<< it->second->nAvailLTEBS<<endl;
            }


            for (std::map<long int, FlowInfoItem*>::iterator it = pallflow->begin(); it != pallflow->end(); it++) {
                std::vector<long int> temp;
                temp.push_back(it->first);


                std::set<int> tset;
                tset.insert(it->second->userIndex);

                std::vector<long int> flowv;
                flowv.push_back(it->first);

                ret = apToFlowSet.insert(std::pair<int, std::vector<long int> >(it->second->nAvailLTEBS, temp));
                assert(it->second->nAvailLTEBS ==0);
                retU = apToUserSet.insert(std::pair<int, std::set<int> >(it->second->nAvailLTEBS, tset));
                retUserFlow = userFlowList.insert(std::pair<int, std::vector<long int> >(it->second->userIndex, flowv));
                if (ret.second == false) {
                    ret.first->second.push_back(it->first);

                }
                if (retU.second == false) {
                    retU.first->second.insert(it->second->userIndex);
                }
                if (retUserFlow.second == false) {
                    retUserFlow.first->second.push_back(it->first);
                }


                assert(it->second->nAvailWiFiAP >0 && it->second->nAvailWiFiAP<20);

                ret = apToFlowSet.insert(std::pair<int, std::vector<long int> >(it->second->nAvailWiFiAP, temp));
                retU = apToUserSet.insert(std::pair<int, std::set<int> >(it->second->nAvailWiFiAP, tset));

                if (ret.second == false) {
                    ret.first->second.push_back(it->first);
                    if(it->second->nAvailWiFiAP ==1)
                    {
                        cout<<"size "<< ret.first->second.size()<<" flow:"<<it->first<<endl;
                    }

                }
                if (retU.second == false) {
                    retU.first->second.insert(it->second->userIndex);
                }
            }
            cerr << "finish counting" << endl;


        }

        double
        FlowScheduler::sumAllLTEWeights() {
            double sum = 0;
            for (map<int, double>::iterator it = lteFlows.begin(); it != lteFlows.end(); it++) {
                sum += it->second;

            }
            return sum;

            //return std::accumulate(lteFlows.begin(), lteFlows.end(), (double) 0);
        }

        void
        tran(int input, int* re, int nbits) {
            for (int i = 0; i < nbits; i++) {
                int one = ((0x01) << i);
                re[i] = (one & input) >> i;
            }
        }



double FlowScheduler::sumOldLteUtility(double newLteWSum)
{
    double sum = 0;
    for(vector<long int>::iterator it= lteFlowIDs.begin(); it != lteFlowIDs.end(); it++)
    {
        if(pallflow->find(*it) == pallflow->end())
        {
            cout<<"find error in sumOldLteUtility"<<endl;
            exit(0);
        }
        sum += (pallflow->find(*it)->second->utility);
    }
    if(newLteWSum ==0)
        return sum;
    else
    {
        double w = sumAllLTEWeights();
        return (sum * (w/(w+newLteWSum)));
    }
}


///this function gets the total user weights connected to wifi
        ///vv is the flow indices, re is the translated configuration, 0
        ///is LTE

        double
        FlowScheduler::calcUtility(int apIndex, vector<long int>*vv, int* re, int nflow, double* lteSumOut, int isWiFiOnly) {

            double u = 0;
        map<int, set<int> >::iterator setit = apToUserSet.find(apIndex);
        if(setit == apToUserSet.end())
        {
            cout<<"can not find any user under ap "<<apIndex<<endl;
            return 0;
        }
            set<int> &userList = setit->second;
            if (userList.size() == 0)
                return 0;


            cerr << "apIndex" << apIndex << endl;

            double* ww = new double[userList.size()];
            double* lw = new double[userList.size()];
            int * nWiFi = new int[userList.size()];

            //cerr << "list size:" << userList.size() << endl;

            for (unsigned int i = 0; i < userList.size(); i++) {
                ww[i] = 0;
                lw[i] = 0;
                nWiFi[i] = 0;
            }

            //for every user, iterate all the flows
            int i = 0;
            for (set<int>::iterator it = userList.begin(); it != userList.end(); it++, i++) {
                bool isWiFi = false;
                bool isLTE = false;

                int userI = *it;
                vector<long int> &flows = userFlowList.find(userI)->second;
                for (vector<long int>::iterator Fit = flows.begin(); Fit != flows.end(); Fit++) {
                    int npos = std::find(vv->begin(), vv->end(), *Fit) - vv->begin();
                    //	    cerr<<"npos"<<npos<<endl;
                    if (re[npos] == 1)
                    {
                        isWiFi = true;
                        (nWiFi[i])++;
                        //cout<<"nWiFi"<<nWiFi[userI]<<endl;
                        //exit(0);
                    }
                    else if (re[npos] == 0)
                        isLTE = true;
                    else {
                        std::cout << "error assign " << npos << " " << re[npos] << std::endl;
                        exit(0);
                    }

                }//for in
                if (isWiFi) {
                    ww[i] = wifiW->find(userI)->second;
                }
                if (isLTE) {
                    cerr << "userI" << userI << endl;
                    assert(userI >= 0);
                    //assert(userI <= 6);
                    lw[i] = lteW->find(userI)->second;
                    //lw[userI] = 1;
                }
            }//for out
            double wifiSum = 0;
            double lteSum = 0;

            for (unsigned int i = 0; i < userList.size(); i++) {
                wifiSum += ww[i];
                //cout<<"ww[i]"<<i<<" "<<ww[i]<<endl;
            }
            for (unsigned int i = 0; i < userList.size(); i++) {
                lteSum += lw[i];
            }

            *lteSumOut = lteSum;


            //for every user 
            i = 0;
            for (set<int>::iterator it = userList.begin(); it != userList.end(); it++, i++) {

                int userI = *it;
                double resourceW = 0;
                double resourceL = 0;

                if (wifiSum != 0) {
                    if(nWiFi[i] >0)
                    {
                    resourceW = ww[i] / wifiSum * ((capMap->find(apIndex)->second) / nWiFi[i]);
                    }
                    else
                    {

                    resourceW = 0;
                    }

                }
                if (lteSum != 0) {
                    resourceL = lw[i] / lteSum * (capMap->find(0)->second);
                }
                assert(!isinf(resourceW));
                assert(!isinf(resourceL));

 


                vector<long int> &flows = userFlowList.find(userI)->second;


                double userWW = 0;
                double userWL = 0;
                for (vector<long int>::iterator Fit = flows.begin(); Fit < flows.end(); Fit++) {
                    int npos = std::find(vv->begin(), vv->end(), *Fit) - vv->begin();
                    if (re[npos] == 1) {
                        userWW += ((pallflow->find(*Fit)->second->weight)* (pallflow->find(*Fit)->second->dSize));
                    } else if (re[npos] == 0) {

                        userWL += ((pallflow->find(*Fit)->second->weight)* (pallflow->find(*Fit)->second->dSize));
                    }

                }//for fit


                for (vector<long int>::iterator Fit = flows.begin(); Fit != flows.end(); Fit++) {
                    //					u += log(resource * ((pallflow->find(*Fit)->second->weight) / userW));
                    double uFlow =0;
                    cerr<<"resourceW " <<resourceW<<" "<< resourceL<<endl;
                    int npos = std::find(vv->begin(), vv->end(), *Fit) - vv->begin();
                    if (re[npos] == 1) {
                        uFlow = (pallflow->find(*Fit)->second->weight) * log(resourceW * ((pallflow->find(*Fit)->second->weight) * (pallflow->find(*Fit)->second->dSize) / userWW));

                        u += uFlow;                         
                        //assert(((pallflow->find(*Fit)->second->weight) * log(resourceW * ((pallflow->find(*Fit)->second->weight) * (pallflow->find(*Fit)->second->dSize) / userWW))) != 0);
                       // cerr << "userww" << userww << endl;
                       // assert(resourceW !=0);
                        //assert((pallflow->find(*Fit)->second->dSize) !=0);
                        //assert((pallflow->find(*Fit)->second->weight) !=0);
                        //assert(!isinf((pallflow->find(*Fit)->second->weight) * log(resourceW * ((pallflow->find(*Fit)->second->weight) * (pallflow->find(*Fit)->second->dSize) / userWW))));
                    } else if (re[npos] == 0) {
                        if(!isWiFiOnly)
                        {
                        uFlow = (pallflow->find(*Fit)->second->weight) * log(resourceL * ((pallflow->find(*Fit)->second->weight) * (pallflow->find(*Fit)->second->dSize) / userWL));
                        u += uFlow;
                        }
                        //assert((pallflow->find(*Fit)->second->weight) * log(resourceL * ((pallflow->find(*Fit)->second->weight) * (pallflow->find(*Fit)->second->dSize) / userWL)) != 0);
                        //assert(!isinf(pallflow->find(*Fit)->second->weight) * log(resourceL * ((pallflow->find(*Fit)->second->weight) * (pallflow->find(*Fit)->second->dSize) / userWL)));
                    }
                    this->uCalc[npos] = uFlow;

                }

            }//for user

            //the weights of lte flows already assigned
            double oldw = sumOldLteUtility(lteSum);
            //cout<< "oldw="<<oldw<<endl;
            if(!isWiFiOnly)
            {
                u += oldw;
            }

                delete[] ww;
                delete[] lw;
                delete[] nWiFi;


            cerr << "u=" << u << endl;
            return u;
        }

    void FlowScheduler::setUtility(vector<long int>* vp, map<int, double>* up)
{
    for(unsigned int i =0; i< vp->size(); i++)
    {
        FlowInfoItem* fp = pallflow->find((*vp)[i])->second;
        if(up->find(i) == up->end())
        {
            cout<<"find err in setUtility"<<endl;
            exit(0);

        }
        fp->utility = (*up)[i];
    }

}

        double FlowScheduler::findMaxConfig(int apIndex, unsigned int* result, int* nflowRe, double* lteSumP) {
            //unsigned int re = 0;
            double value = 0;
            double maxV = 0;

            int nflow = 0;
            vector<long int>& vv = apToFlowSet.find(apIndex)->second;
            cerr << apIndex << "  size of vv" << vv.size() << endl;

            //we can not solve prblem of this scale yet
            assert(vv.size() < 100);



            for (vector<long int>::iterator it = vv.begin(); it != vv.end(); it++) {
                nflow += 1;
            }

           // cerr << "apIndex" << apIndex << " nflow" << nflow << endl;
            assert(nflow >= 0);
            *nflowRe = nflow;

            if (nflow == 0)
                return 0;

            int* re = new int[nflow];

            //uMax.clear();
            uCalc.clear();
            //uCalc.resize(nflow);
         double lteSumF =0;
         double lteSumC =0;

            for (unsigned int i = 0; i <((unsigned int) (1 << nflow)); i++) {
                tran(i, re, nflow);

                value = calcUtility(apIndex, &vv, re, nflow, &lteSumC, 0);

                cerr<<"calc utility of "<<i <<" ="<<value<<endl;
                if (value > maxV) {

                    maxV = value;
                    *result = i;
                    this->uMax.swap(this->uCalc);
                    lteSumF = lteSumC;

                }


            }//for
            cerr << "maxV=" << maxV << " result " << *result << endl;
            //set the utility of vv to uMax
            setUtility(&vv, &uMax);
            *lteSumP = lteSumF;

            delete[] re;
            return maxV;



        }


void FlowScheduler::updateOldUtility(double lteSumF)
{
    for(vector<long int>::iterator it=lteFlowIDs.begin(); it != lteFlowIDs.end(); it++)
    {
        double wall = sumAllLTEWeights();
        if(pallflow->find(*it) == pallflow->end())
        {
            cout<<"find error in updateOldUtility"<<endl;
            exit(0);
        }
        (pallflow->find(*it)->second->utility) *= (wall / (wall + lteSumF)); 

    }//for
}


//input is all the lte flow ids
  double FlowScheduler::calcLteU(vector<long int>* lid)
{

    //this is from userindex to the weight of the user, only for the users to lte
    map<int, double> lteUserW;

    //double is the w_f * size, int is the user index, only calc the user's flow on lte
    map<int, double> fwSum;
    double uLte = 0;
    for(vector<long int>::iterator it= lid->begin(); it != lid->end(); it++)
    {
        map<long int, FlowInfoItem*>::iterator fit = pallflow->find(*it);

        if(fit == pallflow->end())
        {
            cout<<"find user index error in calcLteU"<<endl;
            exit(0);

        }
        int userI = fit->second->userIndex;
        fwSum[userI] += (fit->second->dSize) * (fit->second->weight); 
        map<int, double>::iterator findit = lteW->find(userI);
        if(findit == lteW->end())
        {
            cout<<"error find userI in calcLteU"<<endl;
            exit(0);
        }

        lteUserW[userI] = lteW->find(userI)->second;
    }

    //calc the sum of user weights
    double lteSum=0;
    for(map<int, double>::iterator it= lteUserW.begin(); it != lteUserW.end(); it++)
    {
        lteSum += it->second;
    }


    for(vector<long int>::iterator it= lid->begin(); it != lid->end(); it++)
    {
        map<long int, FlowInfoItem*>::iterator fit = pallflow->find(*it);
        int userI = fit->second->userIndex;
        double uw= lteUserW[userI]; 
        double fw= (fit->second->weight)  * (fit->second->dSize);    
        double through =(uw / lteSum) * (capMap->find(0)->second) *(fw/(fwSum.find(userI)->second));
        assert(through > 0);
        uLte +=log(through) * (fit->second->weight) ; 

    }

    return uLte;


}

void FlowScheduler::makeDecisionsRandom(std::map<int, int>* papcap, std::map<long int, FlowInfoItem*>* pallflow0,
        std::map<int, double>* wifiW0, std::map<int, double>* lteW0, int dryrun)
{
    wifiW = wifiW0;
    lteW = lteW0;
    capMap = papcap;

    pallflow = pallflow0;
    double uAll =0;
    //vector<long int> lteFlowIDs;
    lteFlowIDs.clear();

    divideByCoverage();

    //vector<int> result;
    randomp->SetAttribute("Min", DoubleValue(0));
    randomp->SetAttribute("Max", DoubleValue(100000000));

    //for every wifi ap that has flows

    for (map<int, set<int> >::iterator it = apToUserSet.begin(); it != apToUserSet.end(); it++) {
        int apIndex= it->first;

        vector<long int>& vv = apToFlowSet.find(apIndex)->second;
        cerr << apIndex << "  size of vv" << vv.size() << endl;
        //we can not solve prblem of this scale yet
        assert(vv.size() < 100);

        int nflow =0;

        for (vector<long int>::iterator it = vv.begin(); it != vv.end(); it++) {
            nflow += 1;
        }
        int* plan = new int[nflow];
        for (int i=0; i<nflow; i++) {
            plan[i] = (randomp->GetInteger()) %2;
            if(!dryrun)
            {
                if(plan[i] ==0)
                {
                    pallflow->find(vv[i])->second->nOnNetwork = pallflow->find(vv[i])->second->nAvailLTEBS;
                    lteFlowIDs.push_back(vv[i]);
                }
                else if(plan[i] ==1)
                {
                    pallflow->find(vv[i])->second->nOnNetwork = pallflow->find(vv[i])->second->nAvailWiFiAP;
                }
                else
                {
                    cout<<"plan error "<<i <<"  "<<plan[i]<<endl;
                    exit(0);
                }
            }
        }//for i

        double lteSumO;

        uAll += calcUtility(apIndex, &vv, plan, nflow, &lteSumO, 1);
        delete[] plan;

    }//for every wifi ap
    cout<<"uAll wifi part"<< uAll<<endl;

    double ulte = calcLteU(&lteFlowIDs);

    cout<<"uAll lte part"<< ulte<<endl;
    uAll+= ulte;
    fprintf(ulogFpR, "%.5f\n", uAll);
    cout<<"uAll random "<<uAll<<endl;

}


        ///std::map<int, FlowInfoItem*>* pallflow 
        ///int is the flow ID, FlowInfoItem is the pointer to the flow
        void FlowScheduler::makeDecisions(std::map<int, int>* papcap, std::map<long int, FlowInfoItem*>* pallflow0,
                std::map<int, double>* wifiW0, std::map<int, double>* lteW0)
 {

            wifiW = wifiW0;
            lteW = lteW0;
            capMap = papcap;
            pallflow = pallflow0;

            lteFlows.clear();
            lteFlowIDs.clear();


            std::set<int> toDoList;

            double obMax = 0;
            double uAll = 0;

            for (std::map<long int, FlowInfoItem*>::iterator it = pallflow->begin(); it != pallflow->end(); it++) {
              it->second->utility = -1;
            }
            
            //exit(0);
            divideByCoverage();

            for (map<int, set<int> >::iterator it = apToUserSet.begin(); it != apToUserSet.end(); it++) {
                // cout<<"pushed value o:"<<it->first<<endl;
                if ((it->first) != 0) {
                    //toDoList.push_back(it->first);
                    int a = it->first;
                    toDoList.insert(a);
                }
            }

            cout << "to size" << toDoList.size() << endl;
            //check info


            unsigned int maxConfig = 0;
            unsigned int maxConfigF = 0;
            int maxAPIndex = 0;
            int nflow = 0;
            int nflowF =0;
            //std::map<int, FlowInfoItem*> cflow;
            while (!toDoList.empty()) {
                maxAPIndex = 0;
                obMax = 0;
                maxConfig = 0;


                double lteSumM =0;
                double lteSumC =0;
                for (set<int>::iterator tit = toDoList.begin(); tit != toDoList.end(); tit++) {
		//for every AP, call the function findMaxConfig to find the configuration
            //that maximize the objective, 
            //configuration is as a integer in maxConfig, for details see the definition
                    double v = findMaxConfig(*tit, &maxConfig, &nflow, &lteSumC);
                    if (v < 0) {

                        //toDoList.erase(*it);
                        cerr << *tit << " v negative" << endl;
                        continue;

                    }
                    if (v > obMax) {
                        obMax = v;
                        maxAPIndex = *tit;
                        lteSumM = lteSumC;
                        maxConfigF = maxConfig;
                        nflowF = nflow;
                    }//if
                }//for

                //cerr << "nflow *" << nflow << endl;
                int* re = new int[nflowF];
                for(int ii=0; ii<nflowF; ii++)
                    re[ii] = -1;
                //std::fill(re, re+sizeof(re), -1);
                
                //translate the schedule
                tran(maxConfigF, re, nflowF);

                vector<long int>& vv = apToFlowSet.find(maxAPIndex)->second;
                int jj = 0;

                for (vector<long int>::iterator it = vv.begin(); it != vv.end(); it++, jj++) {
                        fmap::iterator fit = pallflow->find(*it);
                        if(fit == pallflow->end())
                        {
                            cout<<"error find in make decision"<<endl;
                            exit(0);
                        }
                    if (re[jj] == 0) {
                        
                        fit->second->nOnNetwork = fit->second->nAvailLTEBS;
                        int userI = fit->second->userIndex;
                        if(lteW->find(userI) == lteW->end())
                        {
                            cout<<"error find lteW"<<endl;
                            exit(0);
                        }
                        lteFlows[userI] = lteW->find(userI)->second;
                        lteFlowIDs.push_back(*it);

                    } else if (re[jj] == 1) {
                        fit->second->nOnNetwork = fit->second->nAvailWiFiAP;

                    }
                    else
                    {
                        cout<<"re value error "<<jj <<" "<<*it<<" "<<re[jj]<<endl;
                        cout<<"maxconig "<<maxConfigF<<" nflow "<<nflowF<<endl;
                        exit(0);
                    }
                }

                updateOldUtility(lteSumM);

                delete[] re;

                cout << "done " << maxAPIndex << " " << maxConfig << endl;
                //toDoList.erase( std::remove( toDoList.begin(), toDoList.end(), maxAPIndex ), toDoList.end() );
                set<int>::iterator sit = toDoList.find(maxAPIndex);
                cerr << *sit << endl;
                toDoList.erase(toDoList.find(maxAPIndex));
                //remove 
                //		    toDoList.remove(maxAPIndex);
                //cout<<"todolist"<<toDoList.size()<<endl;

            }//while
            cerr << "decision all made" << endl;

           

            for (std::map<long int, FlowInfoItem*>::iterator it = pallflow->begin(); it != pallflow->end(); it++) {
                assert(it->second->utility >0);
                uAll += it->second->utility;

            } 
            cout<<"uAll = "<<uAll<<endl;
            fprintf(ulogFp, "%.5f\n", uAll);
            //remove apMax from the toToList

            //for the next iteration, some flows are set to LTE already, so 

        }
	////////////////////////////////////////////////////////////////////////////////////////////////ljw
int max(int a, int b){
    return a>b?a:b;
}
double transfer2WiFi(std::vector<FlowInfoItem*>& FlowVec, double dAmount){
  std::vector<FlowInfoItem*> transFlow;
  int n = FlowVec.size();
  int w = ((int)dAmount)/100;
  int dp[n+1][w+1];
  memset(dp, 0, sizeof(dp)); 
  int i, j;
  std::cout<<" n "<<n<<" w "<<w<<std::endl;
  for(i=1;i<=n;i++){
     for(j=0;j<=w;j++){
          if(j - ((int)FlowVec[i-1]->dSize)/100 < 0)
              dp[i][j] = dp[i-1][j];
          else
              dp[i][j] = max(dp[i-1][j], dp[i-1][j-((int)FlowVec[i-1]->dSize)/100] + ((int)FlowVec[i-1]->dSize)/100);
     }
  }
  std::cout<<"Transfered "<<dp[n][w]*100<<" of "<<dAmount<<std::endl;

  for(i=n, j=w; i>=1; i--){
     if(j - ((int)FlowVec[i-1]->dSize)/100 >=0 && dp[i][j] ==  dp[i-1][j-((int)FlowVec[i-1]->dSize)/100] + ((int)FlowVec[i-1]->dSize)/100){
         std::cout<<"put in flow "<<FlowVec[i-1]->nFlowId<<" size "<<(int)FlowVec[i-1]->dSize<<std::endl;
         
         FlowVec[i-1]->nAvailLTEBS = -1;
         j = j - ((int)FlowVec[i-1]->dSize)/100;
     }
  }
  //std::cout<<std::endl;
  double dFinalAmount = 0;
  for(i=0; i<n;i++){
     if(FlowVec[i]->nAvailLTEBS == -1){
        FlowVec[i]->nAvailLTEBS = 0;
        FlowVec[i]->nOnNetwork = FlowVec[i]->nAvailWiFiAP;
        dFinalAmount += FlowVec[i]->dSize; 
     }
     else
        FlowVec[i]->nOnNetwork = FlowVec[i]->nAvailLTEBS;
  }
  
 std::cout<<"Final Transfered "<<dFinalAmount<<std::endl<<std::endl;

  
  return dFinalAmount;
}

void FlowScheduler::makeDecisionsEven(std::map<int, int>* papcap, std::map<long int, FlowInfoItem*>* pallflow, std::map<int, double>* psinr, std::map<int, double>* pwifiwt){
    
    std::map<int, double> mapAPSize;
    std::map<int, std::vector<FlowInfoItem*>*> mapAPFlow;
    std::map<long int, FlowInfoItem*>::iterator flit =  pallflow->begin();
    while(flit != pallflow->end()){
       if(mapAPSize.find(flit->second->nAvailLTEBS) != mapAPSize.end()){
          mapAPSize[flit->second->nAvailLTEBS] += flit->second->dSize;
          mapAPFlow[flit->second->nAvailLTEBS]->push_back(flit->second);
       }
       else{
          mapAPSize[flit->second->nAvailLTEBS] = flit->second->dSize;
          std::vector<FlowInfoItem*>* pFlowVec = new std::vector<FlowInfoItem*>;
          pFlowVec->push_back(flit->second);
          mapAPFlow[flit->second->nAvailLTEBS] = pFlowVec;
       }
       
       if(mapAPSize.find(flit->second->nAvailWiFiAP) != mapAPSize.end()){
          mapAPSize[flit->second->nAvailWiFiAP] += flit->second->dSize;
          mapAPFlow[flit->second->nAvailWiFiAP]->push_back(flit->second);
       }
       else{
          mapAPSize[flit->second->nAvailWiFiAP] = flit->second->dSize;
          std::vector<FlowInfoItem*>* pFlowVec = new std::vector<FlowInfoItem*>;
          pFlowVec->push_back(flit->second);
          mapAPFlow[flit->second->nAvailWiFiAP] = pFlowVec;
       }      
       ++flit;
   }
   double dAve = mapAPSize[0]/mapAPSize.size();
   std::map<int, double>::iterator minit, asit;
   asit = mapAPSize.begin(); 
   while(asit != mapAPSize.end()){
       std::cout<<"AP "<<asit->first<<" size "<<asit->second<<std::endl;
       ++asit;
   }

   while(mapAPSize.size()>1){      
      asit = minit = mapAPSize.begin();
      ++asit;
      while(asit != mapAPSize.end()){
         if(asit->second < minit->second){
             minit = asit;
         }
         ++asit;
      }
      double dTransfer;     
      if(minit->second <= dAve){
          dTransfer = minit->second;
      }
      else{
         dTransfer = dAve;
      }
      //transfer dTransfer of asit->first to WiFi
      std::cout<<"@Ave "<<dAve<<" transfer AP "<<minit->first<<" "<<dTransfer<<" of "<<minit->second<<std::endl;
      dTransfer = transfer2WiFi(*mapAPFlow[minit->first], dTransfer);
      mapAPSize.erase(minit);
      dAve = (mapAPSize[0] - dTransfer)/mapAPSize.size();
   }
} 
///////////////////////////////////////////FlowScheduler/////////////////////////////////////////////////

FlowInfoItem::FlowInfoItem(sw_flow_key* key, int onntwk, int avlte, int avwifi, int user):window(5), PACKETSIZE(1024){
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
    utility = -1;
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

