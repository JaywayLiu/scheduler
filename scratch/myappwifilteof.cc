/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011, 2013 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Jaume Nin <jaume.nin@cttc.cat>
 *         Nicola Baldo <nbaldo@cttc.cat>
 */
#include <map>
#include <iostream>
#include <vector>
#include "ns3/lte-helper.h"
#include "ns3/epc-helper.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/wifi-module.h"
#include "ns3/csma-module.h"
#include "ns3/openflow-module.h"
#include "ns3/bridge-module.h"
#include "ns3/config-store-module.h"
#include "ns3/of-controller.h"
#include "ns3/flow-monitor.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/trace-helper.h"

using namespace ns3;
using std::cout;
using std::endl;

/*
 * Simple simulation program using the emulated EPC.
 * For the LTE radio part, it simulates a simple linear topology with
 * a fixed number of eNBs spaced at equal distance, and a fixed number
 * of UEs per each eNB, located at the same position of the eNB. 
 * For the EPC, it uses EmuEpcHelper to realize the S1-U connection
 * via a real link. 
 */

NS_LOG_COMPONENT_DEFINE ("EpcFirstExample");

//<<<<<<< HEAD
void  doSchedule(Ptr<ns3::ofi::MyController> controller, std::map<long int, Ptr<Application> >& pmapFlowApp, double next);
void checkStartEnd(double&, double&, double, double);
//=======
//void  doSchedule(Ptr<ns3::ofi::MyController> controller, std::map<long int, Ptr<Application> >& pmapFlowApp);
void  updateFlowStat(Ptr<ns3::ofi::MyController> controller);

//>>>>>>> bfc9bb7c228cd15613e578516bfebcdc4bb7419f

int
main (int argc, char *argv[])
{
 
  uint16_t nEnbs = 1;
  uint16_t nWiFiAPs = 4;
  uint16_t nUesPerWiFiAp = 5;
  uint16_t stype=0;

  double simTime = 50.1;
  double distance = 1000.0;

  // Command line arguments
  CommandLine cmd;
  cmd.AddValue("schedulerType", "scheduler type, 0 default, 1 random, 2 average", stype);

  cmd.AddValue("nEnbs", "Number of eNBs", nEnbs);
  cmd.AddValue("nWiFiAPs", "Number of WiFi APs", nWiFiAPs);
  cmd.AddValue("nUesPerWiFiAp", "Number of UEs per WiFi APs", nUesPerWiFiAp);


  cmd.AddValue("simTime", "Total duration of the simulation [s]", simTime);
  cmd.AddValue("distance", "Distance between eNBs [m]", distance);
  //cmd.AddValue("interPacketInterval", "Inter packet interval [ms]", interPacketInterval);
  cmd.Parse(argc, argv);

  

  //first create controller and variable declariation
  Ptr<ns3::ofi::MyController> controller = CreateObject<ns3::ofi::MyController>();
  Ptr<OutputStreamWrapper> rtableoutputstream = Create<OutputStreamWrapper> ("rtable", std::ios::out);

  NodeContainer ueNodes;
  ueNodes.Create(nWiFiAPs * nUesPerWiFiAp);
  std::cout<<"UE Id: ";
  for(uint32_t i=0; i<ueNodes.GetN();i++){
     std::cout<<ueNodes.Get(i)->GetId()<<" ";
  }
  std::cout<<std::endl;

  NodeContainer enbNodes;
  enbNodes.Create(nEnbs);
  std::cout<<"eNB Id: ";
  for(uint32_t i=0; i<enbNodes.GetN();i++){
     std::cout<<enbNodes.Get(i)->GetId()<<" ";
  }
  std::cout<<std::endl;

  NodeContainer wifiApNodes;
  wifiApNodes.Create(nWiFiAPs);
  std::cout<<"WiFi AP Id: ";
  for(uint32_t i=0; i<wifiApNodes.GetN();i++){
     std::cout<<wifiApNodes.Get(i)->GetId()<<" ";
  }
  std::cout<<std::endl;

  NodeContainer SinkServers;
  SinkServers.Create(nWiFiAPs +1);
  std::cout<<"Sink Id: ";
  for(uint32_t i=0; i<SinkServers.GetN();i++){
     std::cout<<SinkServers.Get(i)->GetId()<<" ";
  }
  std::cout<<std::endl;

  NodeContainer csmaSwitchs;
  csmaSwitchs.Create (nWiFiAPs +1);
  std::cout<<"CSMA Switch Id: ";
  for(uint32_t i=0; i<csmaSwitchs.GetN();i++){
     std::cout<<csmaSwitchs.Get(i)->GetId()<<" ";
  }
  std::cout<<std::endl;

  Ptr<Node> ueNode; 
  Ptr<Node> remoteHost; 
  Ipv4Address* premoteHostAddr = new Ipv4Address [nWiFiAPs + 1];

  Config::SetDefault("ns3::DropTailQueue::MaxPackets", UintegerValue(10000000));
  CsmaHelper csmahelper;
  csmahelper.SetChannelAttribute ("DataRate", DataRateValue (50000000000));
  csmahelper.SetChannelAttribute ("Delay", TimeValue (NanoSeconds (1))); 
  //csmahelper.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(5000000)); 
  Ipv4AddressHelper ipv4helper;
  OpenFlowSwitchHelper ofswitchhelper;
  InternetStackHelper internethelper;
  Ipv4StaticRoutingHelper ipv4routinghelper;
  
  NetDeviceContainer netDevCt;
  NetDeviceContainer ofSWDevices;
  NetDeviceContainer* pclientDevices =  new NetDeviceContainer[nWiFiAPs +1];
  NetDeviceContainer* pserverDevices =  new NetDeviceContainer[nWiFiAPs +1];
  NetDeviceContainer* pswitchDevices =  new NetDeviceContainer[nWiFiAPs +1]; 

  Ipv4InterfaceContainer apinternetIpIfaces; 
  Ipv4InterfaceContainer serverinternetIpIfaces;

  Ipv4InterfaceContainer ueLteIpIfaces;
  Ipv4InterfaceContainer ueWifiIpIfaces;

  internethelper.Install (SinkServers);
  internethelper.Install (wifiApNodes);
  internethelper.Install (ueNodes);
 
  // Install Mobility Model()
  //LTE eNodeB Mobility
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

  Ptr<ListPositionAllocator> positionAllocLTE = CreateObject<ListPositionAllocator> ();
  positionAllocLTE->Add (Vector(distance, distance, 0));
  mobility.SetPositionAllocator(positionAllocLTE);
  mobility.Install(enbNodes);
  
  //WiFi APs Mobility
  double xShift[8] = {-distance/2, 0, distance/2, distance/2, distance/2, 0, -distance/2, -distance/2};
  double yShift[8] = {distance/2, distance/2, distance/2, 0, -distance/2, -distance/2, -distance/2, 0};
  Ptr<ListPositionAllocator> positionAllocWiFi = CreateObject<ListPositionAllocator> ();
  for(int i=0; i<nWiFiAPs; i++){
      positionAllocWiFi->Add (Vector(distance + xShift[i], distance + yShift[i], 0));
  }
  mobility.SetPositionAllocator(positionAllocWiFi);
  mobility.Install(wifiApNodes);

  //UEs Mobility
  Ptr<UniformRandomVariable> usePosition = CreateObject<UniformRandomVariable> ();
  usePosition->SetAttribute ("Min", DoubleValue (-50));
  usePosition->SetAttribute ("Max", DoubleValue (50));
  Ptr<ListPositionAllocator> positionAllocUE = CreateObject<ListPositionAllocator> ();
  for(int i=0; i<nWiFiAPs; i++){
     for(int j=0; j<nUesPerWiFiAp; j++) 
       positionAllocUE->Add (Vector(distance + xShift[i]+usePosition->GetValue(), distance + yShift[i]+usePosition->GetValue(), 0));
  }
  mobility.SetPositionAllocator(positionAllocUE);
  mobility.Install(ueNodes);

  //Install LTE 
  controller->setAPCap(0,2200000);//MBps
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
  Ptr<PointToPointEpcHelper>  epcHelper = CreateObject<PointToPointEpcHelper> ();
  lteHelper->SetEpcHelper (epcHelper);
  epcHelper->Initialize ();

  NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice (enbNodes);
  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice (ueNodes);
 
  ueLteIpIfaces = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueLteDevs));
  Ptr<Node> pgw = epcHelper->GetPgwNode ();
  std::cout<<"PGW Id: "<<pgw->GetId()<<std::endl;

  remoteHost = SinkServers.Get(0);

  for(uint32_t u = 0; u < ueNodes.GetN (); ++u){ 
      ueNode = ueNodes.Get (u);
      // Set the default gateway for the UE
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4routinghelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
  }
  lteHelper->Attach (ueLteDevs, enbLteDevs.Get(0)); 


  // Create the csma links, from each terminal to the switch
  netDevCt = csmahelper.Install (NodeContainer (remoteHost, csmaSwitchs.Get(0)));
  pserverDevices[0].Add (netDevCt.Get (0));
  pswitchDevices[0].Add (netDevCt.Get (1));

  netDevCt = csmahelper.Install (NodeContainer (pgw, csmaSwitchs.Get(0)));
  pclientDevices[0].Add (netDevCt.Get (0));
  pswitchDevices[0].Add (netDevCt.Get (1));

  //create ofswitch
  ofSWDevices.Add(ofswitchhelper.Install (csmaSwitchs.Get (0), pswitchDevices[0], controller));

  ipv4helper.SetBase ("10.1.0.0", "255.255.255.0");
  serverinternetIpIfaces.Add(ipv4helper.Assign (pserverDevices[0])); 
  apinternetIpIfaces.Add(ipv4helper.Assign (pclientDevices[0]));
  premoteHostAddr[0]= serverinternetIpIfaces.GetAddress (0); 
//map<long int, FlowInfoItem*>::iterator fit = pallflow->find(*it);

  //Create WiFi
  std::vector<NetDeviceContainer> staDevices;
  std::vector<NetDeviceContainer> apDevices;
 
  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default ();
  wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO); 

  for(int i=0; i<nWiFiAPs; i++){
    //controller->setAPCap(i+1,3000000);//MBps
    controller->setAPCap(i+1,1800000);//MBps
    std::ostringstream oss;
    oss << "wifi-default-" << i;
    Ssid ssid = Ssid (oss.str ());

    WifiHelper wifi = WifiHelper::Default ();
    wifi.SetRemoteStationManager ("ns3::AarfWifiManager");
    NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();

    YansWifiChannelHelper wifichannel = YansWifiChannelHelper::Default ();
    wifiPhy.SetChannel (wifichannel.Create ());    

    wifiMac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
    NetDeviceContainer apDev;
    apDev = wifi.Install (wifiPhy, wifiMac, wifiApNodes.Get(i));

    NodeContainer ueOfWiFiAP;
    for(int j=0; j<nUesPerWiFiAp; j++){
       ueOfWiFiAP.Add(ueNodes.Get(i*nUesPerWiFiAp + j));
    }

    wifiMac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid),  "ActiveProbing", BooleanValue (false));
    NetDeviceContainer staDev;
    staDev = wifi.Install (wifiPhy, wifiMac, ueOfWiFiAP);

    // Create the csma links, from each terminal to the switch ueWifiIpIfaces
    remoteHost = SinkServers.Get(i+1);
    netDevCt = csmahelper.Install (NodeContainer (remoteHost, csmaSwitchs.Get(i+1)));
    pserverDevices[i+1].Add (netDevCt.Get (0));
    pswitchDevices[i+1].Add (netDevCt.Get (1));

    netDevCt = csmahelper.Install (NodeContainer (wifiApNodes.Get(i), csmaSwitchs.Get(i+1)));
    pclientDevices[i+1].Add (netDevCt.Get (0));
    pswitchDevices[i+1].Add (netDevCt.Get (1));
  
    //create ofswitch
    ofSWDevices.Add(ofswitchhelper.Install (csmaSwitchs.Get (i+1), pswitchDevices[i+1], controller));

    //wifi ap + server ip
    std::ostringstream ipss;
    ipss<<"10.1."<<(i+1)<<".0";
    ipv4helper.SetBase (ipss.str().c_str(), "255.255.255.0");
    serverinternetIpIfaces.Add(ipv4helper.Assign (pserverDevices[i+1])); 
    apinternetIpIfaces.Add(ipv4helper.Assign (pclientDevices[i+1]));
    premoteHostAddr[i+1]= serverinternetIpIfaces.GetAddress (i+1); 
 
    //wifi sta ip
    std::ostringstream ipss2;
    ipss2<<"10.2."<<(i+1)<<".0";
    ipv4helper.SetBase (ipss2.str().c_str(), "255.255.255.0");
    ueWifiIpIfaces.Add(ipv4helper.Assign (staDev));
    Ipv4InterfaceContainer apIface = ipv4helper.Assign (apDev);

    for (uint32_t u = 0; u < nUesPerWiFiAp; ++u)
    {
      ueNode = ueOfWiFiAP.Get (u);
      // Set the route for wifi sink
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4routinghelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->AddNetworkRouteTo (Ipv4Address (ipss.str().c_str()), Ipv4Mask ("255.255.255.0"), Ipv4Address (apIface.GetAddress(0)), 2);
      ueStaticRouting->PrintRoutingTable(rtableoutputstream);
    }

    staDevices.push_back(staDev);
    apDevices.push_back(apDev);
  }

  for(int i=0; i<nWiFiAPs+1;i++){
    std::cout<<"AP "<<i<<" Client IP ";
    apinternetIpIfaces.GetAddress(i).Print(std::cout);
    std::cout<<" Server IP " ;
    serverinternetIpIfaces.GetAddress(i).Print(std::cout);
    std::cout<<" OFSwitch "<<GetPointer(ofSWDevices.Get(i)) ;
    controller->setSwitchAP(dynamic_cast<OpenFlowSwitchNetDevice*>(GetPointer(ofSWDevices.Get(i))), i);
    controller->setAPSwitch(i, dynamic_cast<OpenFlowSwitchNetDevice*>(GetPointer(ofSWDevices.Get(i))));
    std::cout<<std::endl;
  }
 
  for(int i=0; i<nWiFiAPs*nUesPerWiFiAp; i++){
    std::cout<<"UE "<<i<<" LTE IP ";
    ueLteIpIfaces.GetAddress(i).Print(std::cout);
    std::cout<<" WiFi IP " ;
    ueWifiIpIfaces.GetAddress(i).Print(std::cout);
    std::cout<<std::endl;
    controller->setSrcIPWifi(ueLteIpIfaces.GetAddress(i).Get(), i/nUesPerWiFiAp+1);
    controller->setSrcIPWifi(ueWifiIpIfaces.GetAddress(i).Get(), i/nUesPerWiFiAp+1);
    controller->setSrcIPUser(ueLteIpIfaces.GetAddress(i).Get(), i);
    controller->setSrcIPUser(ueWifiIpIfaces.GetAddress(i).Get(), i);
  }

  // randomize a bit start times to avoid simulation artifacts
  // (e.g., buffer overflows due to packet transmissions happening
  // exactly at the same time) 
  Ptr<UniformRandomVariable> NoFLowPerUser = CreateObject<UniformRandomVariable> ();
  NoFLowPerUser->SetAttribute ("Min", DoubleValue (2));
  NoFLowPerUser->SetAttribute ("Max", DoubleValue (5));

  Ptr<UniformRandomVariable> flowStartSeconds = CreateObject<UniformRandomVariable> ();
  flowStartSeconds->SetAttribute ("Min", DoubleValue (0.2));
  flowStartSeconds->SetAttribute ("Max", DoubleValue (simTime/2));

  Ptr<UniformRandomVariable> flowLenSeconds = CreateObject<UniformRandomVariable> ();
  flowLenSeconds->SetAttribute ("Min", DoubleValue (3));
  flowLenSeconds->SetAttribute ("Max", DoubleValue (simTime));

  //Ptr<UniformRandomVariable> flowSizebps = CreateObject<UniformRandomVariable> ();
  Ptr<NormalRandomVariable> flowSizebps = CreateObject<NormalRandomVariable> ();
  //ljw size random
flowSizebps->SetAttribute ("Mean", DoubleValue (2000000));
flowSizebps->SetAttribute ("Variance", DoubleValue (1000000));
flowSizebps->SetAttribute ("Bound", DoubleValue (5000000));

  //flowSizebps->SetAttribute ("Min", DoubleValue (1000000));
  //flowSizebps->SetAttribute ("Max", DoubleValue (3000000));
  

  controller->setUENumber(nWiFiAPs * nUesPerWiFiAp);
  controller->setDefaultSINR();
  //stype = 2;
  cout<< "stype = "<<stype<<endl;
  controller->setSType(stype);


  int nFlSize;
  double dFlStart, dFlLen; 
  // Install and start applications on UEs and remote host
 
  //Ptr<Application> anApp;
  double interval = 5;
  int nTotalFlSize = 0;
  uint16_t ulPort = 20000;
  ApplicationContainer ueApp;
  ApplicationContainer serverApps;
  std::map<long int, Ptr<Application> > mapFlowApp;//map from flow id to Application, for enforce scheduling
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u) {
      std::cout<<"UE "<<u<<" App dest ";
      premoteHostAddr[0].Print(std::cout);
      std::cout<<" ";
      premoteHostAddr[u/nUesPerWiFiAp+1].Print(std::cout);
      std::cout<<std::endl;
      int nAppPerUser = NoFLowPerUser->GetInteger();
      uint16_t brPort = ulPort;
      for(int i=0; i<nAppPerUser; i++) {    

         dFlStart = flowStartSeconds->GetValue();
         dFlLen = flowLenSeconds->GetValue();
         dFlLen = (dFlStart + dFlLen)>simTime?simTime-dFlStart-0.2:dFlLen;
         nFlSize = flowSizebps->GetInteger();
         nTotalFlSize += nFlSize;
         checkStartEnd(dFlStart, dFlLen, simTime,interval);

         controller->setOrgFlow(0, dFlStart, dFlLen, nFlSize);
         MyOnOffHelper appHelper ("ns3::UdpSocketFactory", brPort, InetSocketAddress(premoteHostAddr[0],brPort), brPort+1, 
                                  InetSocketAddress(premoteHostAddr[u/nUesPerWiFiAp+1],brPort+1));
         appHelper.SetConstantRate(nFlSize, 1024);
         ueApp = appHelper.Install(ueNodes.Get(u));
         ueApp.Get(0)->SetStartTime(Seconds(dFlStart));
         ueApp.Get(0)->SetStopTime(Seconds(dFlStart+dFlLen));
         uint32_t srcIP = ueLteIpIfaces.GetAddress(u).Get();
         long int nflid = (((srcIP>>8)&255)*100 + (srcIP&255))*100000 + brPort;
         mapFlowApp[nflid] = ueApp.Get(0);

         srcIP = ueWifiIpIfaces.GetAddress(u).Get();
         long int nflid2 = (((srcIP>>8)&255)*100 + (srcIP&255))*100000 + brPort+1;
         mapFlowApp[nflid2] = ueApp.Get(0);
      
         PacketSinkHelper ltePacketSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), brPort));
         serverApps.Add (ltePacketSinkHelper.Install (SinkServers.Get(0)));
         PacketSinkHelper wifiPacketSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), brPort+1));
         serverApps.Add (wifiPacketSinkHelper.Install (SinkServers.Get(u/nUesPerWiFiAp+1))); 
         brPort+=10;

         std::cout<<"Flow "<<nflid<<" "<<nflid2<<" from "<<dFlStart<<" till "<<dFlStart+dFlLen<<" size "<<nFlSize<<std::endl;
      }   
      ulPort+=1000;
  }
  std::cout<<"Total Size  "<<nTotalFlSize<<std::endl;
  serverApps.Start (Seconds(0.1));

  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.InstallAll();

//*/
  Simulator::Schedule(Seconds(interval), doSchedule, controller, mapFlowApp, interval);
  Simulator::Schedule(Seconds(interval/2.0f), updateFlowStat, controller);

  //AsciiTraceHelper asciihelper;
  //csmahelper.EnableAsciiAll(asciihelper.CreateFileStream("csma.tr"));
  //wifiPhy.EnableAsciiAll(asciihelper.CreateFileStream("wifi.tr"));

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  flowMonitor->SerializeToXmlFile("flowstat", true, true);
  Simulator::Destroy();
  return 0;
}

void  doSchedule(Ptr<ns3::ofi::MyController> controller, std::map<long int, Ptr<Application> >& pmapFlowApp, double next){
   controller->doScheduling();
   
   //iterate flow to enfore scheduling
   std::map<long int, ns3::ofi::FlowInfoItem*>* pMapFlow = controller->getAllFlowMap();
   std::map<long int, ns3::ofi::FlowInfoItem*>::iterator fit = pMapFlow->begin();

   while(fit!= pMapFlow->end()){
     MyOnOffApplication* pApp = dynamic_cast<MyOnOffApplication*>(GetPointer(pmapFlowApp[fit->second->nFlowId]));
     std::cout<<"Enforce flow "<<fit->first<<" on "<<fit->second->nOnNetwork<<std::endl;
     pApp->SetNetwork(fit->second->nOnNetwork);
     ++fit;
   }   
//<<<<<<< HEAD
   Simulator::Schedule(Seconds(next), doSchedule, controller, pmapFlowApp, next);
//=======
  // Simulator::Schedule(Seconds(2.5),updateFlowStat , controller, true);
  // Simulator::Schedule(Seconds(5), doSchedule, controller, pmapFlowApp);
//>>>>>>> bfc9bb7c228cd15613e578516bfebcdc4bb7419f
}

void updateFlowStat(Ptr<ns3::ofi::MyController> controller)
{
   std::cout<<"@"<<Simulator::Now()<<" Do updateFlowStat"<<std::endl;
   controller->updateFlowStat(true);
   Simulator::Schedule(Seconds(5), updateFlowStat, controller);
}

void checkStartEnd(double& start, double& len, double simtime, double interval){
     Ptr<UniformRandomVariable> randomnb = CreateObject<UniformRandomVariable> ();
     randomnb->SetAttribute ("Min", DoubleValue (0.1));
     randomnb->SetAttribute ("Max", DoubleValue (interval/3));
     double end = start+len;

     //std::cout<<"Origin interval "<<interval<<" start "<<start<<" end "<<end;
     int t = (int)(start/interval);   
     std::cout<<" start t "<<t<<" change to ";
     if((start - t*interval)<interval/2){
        start = (t+0.5)*interval + randomnb->GetValue();
     }
     //std::cout<<start<<" ";
    
     t = (int)(end/interval);
     //std::cout<<" end t "<<t<<" change to ";
     if((end - t*interval)<interval/2){
        end = (t+0.5)*interval + randomnb->GetValue();
        if(end>=simtime)
          end = simtime - 0.2;
     }
     //std::cout<<end<<" "<<std::endl;
     len = end - start;
}

