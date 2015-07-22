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

#include <iostream>
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

using namespace ns3;

/*
 * Simple simulation program using the emulated EPC.
 * For the LTE radio part, it simulates a simple linear topology with
 * a fixed number of eNBs spaced at equal distance, and a fixed number
 * of UEs per each eNB, located at the same position of the eNB. 
 * For the EPC, it uses EmuEpcHelper to realize the S1-U connection
 * via a real link. 
 */

NS_LOG_COMPONENT_DEFINE ("EpcFirstExample");

void makeSchedule(ApplicationContainer ueApps);

int
main (int argc, char *argv[])
{

  uint16_t nEnbs = 1;
  uint16_t nUesPerEnb = 5;
  double simTime = 10.1;
  double distance = 1000.0;
  double interPacketInterval = 1000;

  // Command line arguments
  CommandLine cmd;
  cmd.AddValue("nEnbs", "Number of eNBs", nEnbs);
  cmd.AddValue("nUesPerEnb", "Number of UEs per eNB", nUesPerEnb);
  cmd.AddValue("simTime", "Total duration of the simulation [s]", simTime);
  cmd.AddValue("distance", "Distance between eNBs [m]", distance);
  cmd.AddValue("interPacketInterval", "Inter packet interval [ms]", interPacketInterval);
  cmd.Parse(argc, argv);

  // let's go in real time
  // NOTE: if you go in real time I strongly advise to use
  // --ns3::RealtimeSimulatorImpl::SynchronizationMode=HardLimit
  // I've seen that if BestEffort is used things can break
  // (even simple stuff such as ARP)
  //GlobalValue::Bind ("SimulatorImplementationType", 
  //                 StringValue ("ns3::RealtimeSimulatorImpl"));

  //NS_LOG_COMPONENT_DEFINE ("WiFiLTETest");
  //LogComponentEnable("MyOnOffApplication", LOG_LEVEL_INFO);

  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
  Ptr<PointToPointEpcHelper>  epcHelper = CreateObject<PointToPointEpcHelper> ();
  lteHelper->SetEpcHelper (epcHelper);
  epcHelper->Initialize ();

  Ptr<Node> pgw = epcHelper->GetPgwNode ();

   // Create a single RemoteHost
  //NS_LOG_INFO ("Create servers.");
  NodeContainer lteServer;
  lteServer.Create (1);
  Ptr<Node> remoteHost = lteServer.Get (0);
  InternetStackHelper internet;
  internet.Install (lteServer);
  
  NodeContainer csmaSwitch;
  csmaSwitch.Create (1);

  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", DataRateValue (5000000));
  csma.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));
  // Create the csma links, from each terminal to the switch
  NetDeviceContainer clientDevices;
  NetDeviceContainer serverDevices;
  NetDeviceContainer switchDevices;

  NetDeviceContainer link = csma.Install (NodeContainer (lteServer, csmaSwitch));
  serverDevices.Add (link.Get (0));
  switchDevices.Add (link.Get (1));

  NetDeviceContainer pgwlink = csma.Install (NodeContainer (pgw, csmaSwitch));
  clientDevices.Add (pgwlink.Get (0));
  switchDevices.Add (pgwlink.Get (1));

  Ptr<Node> switchNode = csmaSwitch.Get (0);

  OpenFlowSwitchHelper swtch;

  Ptr<ns3::ofi::MyController> controller = CreateObject<ns3::ofi::MyController>();

  swtch.Install (switchNode, switchDevices, controller);

  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (serverDevices);
  Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress (0);  
  Ipv4InterfaceContainer pgwinternetIpIfaces = ipv4h.Assign (clientDevices);

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), "10.1.1.2", 1);
  

  Ptr<OutputStreamWrapper> outputstream = Create<OutputStreamWrapper> ("rtable", std::ios::out);
  remoteHostStaticRouting->PrintRoutingTable(outputstream);

  //Ipv4GlobalRoutingHelper::PopulateRoutingTables();
  //Install LTE
  NodeContainer ueNodes;
  NodeContainer enbNodes;
  enbNodes.Create(nEnbs);
  ueNodes.Create(nEnbs*nUesPerEnb);


  // Install Mobility Model
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  for (uint16_t i = 0; i < nEnbs; i++)
    {
      positionAlloc->Add (Vector(distance * i, 0, 0));
    }
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.SetPositionAllocator(positionAlloc);
  mobility.Install(enbNodes);
  mobility.Install(ueNodes);

  // Install LTE Devices to the nodes
  NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice (enbNodes);
  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice (ueNodes);

  // Install the IP stack on the UEs
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueLteDevs));
     

 // addstaticroutes
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      Ptr<Node> ueNode = ueNodes.Get (u);
      // Set the default gateway for the UE
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

  lteHelper->Attach (ueLteDevs, enbLteDevs.Get(0)); 
  // side effects: 1) use idle mode cell selection, 2) activate default EPS bearer

  //WiFi interface part
  NodeContainer wifiServer;
  wifiServer.Create (1);
  Ptr<Node> wifiremoteHost = wifiServer.Get (0);
  internet.Install (wifiServer);
  
  NodeContainer wifiApNode;
  wifiApNode.Create(1);
  internet.Install (wifiApNode);

  NodeContainer wificsmaSwitch;
  wificsmaSwitch.Create (1);

  // Create the csma links, from each terminal to the switch
  NetDeviceContainer wificlientDevices;
  NetDeviceContainer wifiserverDevices;
  NetDeviceContainer wifiswitchDevices;

  NetDeviceContainer wifilink = csma.Install (NodeContainer (wifiServer, wificsmaSwitch));
  wifiserverDevices.Add (wifilink.Get (0));
  wifiswitchDevices.Add (wifilink.Get (1));

  NetDeviceContainer wifiapink = csma.Install (NodeContainer (wifiApNode, wificsmaSwitch));
  wificlientDevices.Add (wifiapink.Get (0));
  wifiswitchDevices.Add (wifiapink.Get (1));

  Ptr<Node> wifiswitchNode = wificsmaSwitch.Get (0);


  OpenFlowSwitchHelper wifiswtch;

  //Ptr<ns3::ofi::LearningController> wificontroller = CreateObject<ns3::ofi::LearningController>();

  wifiswtch.Install (wifiswitchNode, wifiswitchDevices, controller);


  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper phy = YansWifiPhyHelper::Default ();
  phy.SetChannel (channel.Create ());
  WifiHelper wifi = WifiHelper::Default ();
  wifi.SetRemoteStationManager ("ns3::AarfWifiManager");
  NqosWifiMacHelper mac = NqosWifiMacHelper::Default ();

  Ssid ssid = Ssid ("ns-3-ssid");
  mac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid),  "ActiveProbing", BooleanValue (false));
  NetDeviceContainer staDevices;
  staDevices = wifi.Install (phy, mac, ueNodes);

  mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
  NetDeviceContainer apDevices;
  apDevices = wifi.Install (phy, mac, wifiApNode);

  ipv4h.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer wifip2pInterfaces = ipv4h.Assign (wifiserverDevices);
  Ipv4Address wifiremoteHostAddr = wifip2pInterfaces.GetAddress (0);
  Ipv4InterfaceContainer wifiapp2pInterfaces = ipv4h.Assign (wificlientDevices);

  //stack install for wifi nodes aready done in lte part
  Ipv4InterfaceContainer staInterfacesWiFi;
  ipv4h.SetBase ("10.1.4.0", "255.255.255.0");
  staInterfacesWiFi = ipv4h.Assign (staDevices);
  ipv4h.Assign (apDevices);


  Ptr<Ipv4StaticRouting> wifiremoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting (wifiremoteHost->GetObject<Ipv4> ());
  wifiremoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("10.1.4.0"), Ipv4Mask ("255.255.255.0"), "10.1.3.2", 1);

  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      Ptr<Node> ueNode = ueNodes.Get (u);
      // Set the route for wifi sink
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->AddNetworkRouteTo (Ipv4Address ("10.1.3.0"), Ipv4Mask ("255.255.255.0"), Ipv4Address ("10.1.4.6"), 2);
      ueStaticRouting->PrintRoutingTable(outputstream);
    }


  //mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiApNode);

  // randomize a bit start times to avoid simulation artifacts
  // (e.g., buffer overflows due to packet transmissions happening
  // exactly at the same time) 
  Ptr<UniformRandomVariable> startTimeSeconds = CreateObject<UniformRandomVariable> ();
  startTimeSeconds->SetAttribute ("Min", DoubleValue (0));
  startTimeSeconds->SetAttribute ("Max", DoubleValue (interPacketInterval/1000.0));


  // Install and start applications on UEs and remote host
  uint16_t ulPort = 34160;
  ApplicationContainer ueApps;
  ApplicationContainer serverApps;
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {

      MyOnOffHelper appHelper ("ns3::UdpSocketFactory", InetSocketAddress(remoteHostAddr,ulPort), InetSocketAddress(wifiremoteHostAddr,ulPort+1));
      appHelper.SetConstantRate(100000, 1024);
      ueApps.Add(appHelper.Install(ueNodes.Get(u)));

      
      PacketSinkHelper ltePacketSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), ulPort));
      serverApps.Add (ltePacketSinkHelper.Install (remoteHost));
      PacketSinkHelper wifiPacketSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), ulPort+1));
      serverApps.Add (wifiPacketSinkHelper.Install (wifiremoteHost));


      MyOnOffHelper appHelper2 ("ns3::UdpSocketFactory", InetSocketAddress(remoteHostAddr,ulPort+10), InetSocketAddress(wifiremoteHostAddr,ulPort+11));
      appHelper2.SetConstantRate(100000, 1024);
      ueApps.Add(appHelper2.Install(ueNodes.Get(u)));

      
      PacketSinkHelper ltePacketSinkHelper2 ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), ulPort+10));
      serverApps.Add (ltePacketSinkHelper2.Install (remoteHost));
      PacketSinkHelper wifiPacketSinkHelper2 ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), ulPort+11));
      serverApps.Add (wifiPacketSinkHelper2.Install (wifiremoteHost));
  
  
      ulPort+=1000;
    }
  ueApps.Start (Seconds (startTimeSeconds->GetValue ()));
  serverApps.Start (Seconds (startTimeSeconds->GetValue ()));
//*/
  Simulator::Schedule(Seconds(5), makeSchedule, ueApps);
  csma.EnablePcapAll("openflowswitch");
  //lteHelper.EnablePcapAll("epc");
  //wifip2ph.EnablePcapAll("ltewifitestof");
  //wifi.EnablePcapAll("ltewifitest");
  phy.EnablePcap("wifi", staDevices.Get(0));
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  Simulator::Destroy();
  return 0;
}


void makeSchedule(ApplicationContainer ueApps)
{
  std::cout<<"Now 5"<<std::endl;
  for (uint32_t u = 0; u < ueApps.GetN ()/2; ++u)
   {
     MyOnOffApplication* appPoint = dynamic_cast<MyOnOffApplication*>(GetPointer(ueApps.Get(u)));
     appPoint->SwitchNetwork();
   }
}


