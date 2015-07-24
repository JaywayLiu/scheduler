#define __STDC_FORMAT_MACROS
#include "of-controller.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/ipv4-list-routing.h"


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
    updateFlowStat();
    pmyScheduler->makeDecisions(&mapAPCap, &mapAllFlows, &mapSINR, &mapWifiWt);
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


///this function gets the total user weights connected to wifi
        ///vv is the flow indices, re is the translated configuration, 0
        ///is LTE

        double
        FlowScheduler::calcUtility(int apIndex, vector<long int>*vv, int* re, int nflow) {

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
           
            //cerr << "list size:" << userList.size() << endl;

            for (unsigned int i = 0; i < userList.size(); i++) {
                ww[i] = 0;
                lw[i] = 0;
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
                        isWiFi = true;
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


            //for every user 
            i = 0;
            for (set<int>::iterator it = userList.begin(); it != userList.end(); it++, i++) {

                int userI = *it;
                double resourceW = 0;
                double resourceL = 0;

                if (wifiSum != 0) {
                    resourceW = ww[i] / wifiSum * (capMap->find(apIndex)->second);
                    //if(resourceW ==0)
                    //{
                        //cout<<"resource! "<<ww[i]<< " "<<wifiSum<<" "<<(capMap->find(apIndex)->second)<<endl;
                   // }
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
                        uFlow = (pallflow->find(*Fit)->second->weight) * log(resourceL * ((pallflow->find(*Fit)->second->weight) * (pallflow->find(*Fit)->second->dSize) / userWL));
                        u += uFlow;                         
                        //assert((pallflow->find(*Fit)->second->weight) * log(resourceL * ((pallflow->find(*Fit)->second->weight) * (pallflow->find(*Fit)->second->dSize) / userWL)) != 0);
                        //assert(!isinf(pallflow->find(*Fit)->second->weight) * log(resourceL * ((pallflow->find(*Fit)->second->weight) * (pallflow->find(*Fit)->second->dSize) / userWL)));
                    }
                    this->uCalc[npos] = uFlow;

                }

            }//for user
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

        double FlowScheduler::findMaxConfig(int apIndex, unsigned int* result, int* nflowRe) {
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

            for (unsigned int i = 0; i <((unsigned int) (1 << nflow)); i++) {
                tran(i, re, nflow);

                value = calcUtility(apIndex, &vv, re, nflow);

                cerr<<"calc utility of "<<i <<" ="<<value<<endl;
                if (value > maxV) {

                    maxV = value;
                    *result = i;
                    this->uMax.swap(this->uCalc);

                }


            }//for
            cerr << "maxV=" << maxV << " result " << *result << endl;
            //set the utility of vv to uMax
            setUtility(&vv, &uMax); 

            delete[] re;
            return maxV;



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


            std::set<int> toDoList;

            double obMax = 0;
            double uAll = 0;
            
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
            int maxAPIndex = 0;
            int nflow = 0;
            //std::map<int, FlowInfoItem*> cflow;
            while (!toDoList.empty()) {
                maxAPIndex = 0;
                obMax = 0;
                maxConfig = 0;
                for (set<int>::iterator tit = toDoList.begin(); tit != toDoList.end(); tit++) {
		//for every AP, call the function findMaxConfig to find the configuration
            //that maximize the objective, 
            //configuration is as a integer in maxConfig, for details see the definition
                    double v = findMaxConfig(*tit, &maxConfig, &nflow);
                    //exit(0);
                    if (v < 0) {

                        //toDoList.erase(*it);
                        cerr << *tit << " v negative" << endl;
                        continue;

                    }
                    if (v > obMax) {
                        obMax = v;
                        maxAPIndex = *tit;
                    }//if
                }//for

                //cerr << "nflow *" << nflow << endl;
                int* re = new int[nflow];
                
                //translate the schedule
                tran(maxConfig, re, nflow);

                vector<long int>& vv = apToFlowSet.find(maxAPIndex)->second;
                int jj = 0;

                for (vector<long int>::iterator it = vv.begin(); it != vv.end(); it++, jj++) {
                    if (re[jj] == 0) {
                        pallflow->find(*it)->second->nOnNetwork = pallflow->find(*it)->second->nAvailLTEBS;
                        int userI = pallflow->find(*it)->second->userIndex;
                        lteFlows[userI] = lteW->find(userI)->second;

                    } else if (re[jj] == 1) {
                        pallflow->find(*it)->second->nOnNetwork = pallflow->find(*it)->second->nAvailWiFiAP;

                    }
                }

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
                uAll += it->second->utility;

            } 
            cout<<"uAll = "<<uAll<<endl;
            fprintf(ulogFp, "%.5f\n", uAll);
            //remove apMax from the toToList

            //for the next iteration, some flows are set to LTE already, so 

        }
	////////////////////////////////////////////////////////////////////////////////////////////////ljw
/*

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

*/
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

