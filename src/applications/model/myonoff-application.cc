/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
//
// Copyright (c) 2006 Georgia Tech Research Corporation
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Author: George F. Riley<riley@ece.gatech.edu>
//

// ns3 - On/Off Data Source Application class
// George F. Riley, Georgia Tech, Spring 2007
// Adapted from ApplicationOnOff in GTNetS.

#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/packet-socket-address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/data-rate.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "myonoff-application.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include <iostream>
namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MyOnOffApplication");

NS_OBJECT_ENSURE_REGISTERED (MyOnOffApplication);

TypeId
MyOnOffApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MyOnOffApplication")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<MyOnOffApplication> ()
    .AddAttribute ("DataRate", "The data rate in on state.",
                   DataRateValue (DataRate ("500kb/s")),
                   MakeDataRateAccessor (&MyOnOffApplication::m_cbrRate),
                   MakeDataRateChecker ())
    .AddAttribute ("PacketSize", "The size of packets sent in on state",
                   UintegerValue (512),
                   MakeUintegerAccessor (&MyOnOffApplication::m_pktSize),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("LteRemote", "The address of the LTE destination",
                   AddressValue (),
                   MakeAddressAccessor (&MyOnOffApplication::m_lte_peer),
                   MakeAddressChecker ())
    .AddAttribute ("WifiRemote", "The address of the WiFi destination",
                   AddressValue (),
                   MakeAddressAccessor (&MyOnOffApplication::m_wifi_peer),
                   MakeAddressChecker ())
    /*.AddAttribute ("OnTime", "A RandomVariableStream used to pick the duration of the 'On' state.",
                   StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                   MakePointerAccessor (&OnOffApplication::m_onTime),
                   MakePointerChecker <RandomVariableStream>())
    .AddAttribute ("OffTime", "A RandomVariableStream used to pick the duration of the 'Off' state.",
                   StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                   MakePointerAccessor (&OnOffApplication::m_offTime),
                   MakePointerChecker <RandomVariableStream>())*/
    .AddAttribute ("MaxBytes", 
                   "The total number of bytes to send. Once these bytes are sent, "
                   "no packet is sent again, even in on state. The value zero means "
                   "that there is no limit.",
                   UintegerValue (0),
                   MakeUintegerAccessor (&MyOnOffApplication::m_maxBytes),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Protocol", "The type of protocol to use.",
                   TypeIdValue (UdpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&MyOnOffApplication::m_tid),
                   MakeTypeIdChecker ())
    .AddTraceSource ("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&MyOnOffApplication::m_txTrace),
                     "ns3::Packet::TracedCallback")
  ;
  return tid;
}


MyOnOffApplication::MyOnOffApplication ()
  : m_current_on (0),
    m_lte_socket (0),
    m_wifi_socket (0),
    m_current_socket (0),
    m_lte_connected (false),
    m_wifi_connected (false),
    m_lte_residualBits (0),
    m_wifi_residualBits (0),
    m_lastStartTime (Seconds (0)),
    m_totBytes (0)
{
  NS_LOG_FUNCTION (this);
}

MyOnOffApplication::~MyOnOffApplication()
{
  NS_LOG_FUNCTION (this);
}

void 
MyOnOffApplication::SetMaxBytes (uint32_t maxBytes)
{
  NS_LOG_FUNCTION (this << maxBytes);
  m_maxBytes = maxBytes;
}

Ptr<Socket>
MyOnOffApplication::GetLteSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_lte_socket;
}

Ptr<Socket>
MyOnOffApplication::GetWifiSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_wifi_socket;
}

int64_t 
MyOnOffApplication::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  //m_onTime->SetStream (stream);
  //m_offTime->SetStream (stream + 1);
  return 2;
}

void
MyOnOffApplication::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  m_lte_socket = 0;
  m_wifi_socket = 0;
  // chain up
  Application::DoDispose ();
}

// Application Methods
void MyOnOffApplication::StartApplication () // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);

  // Create the socket if not already
  if (!m_lte_socket)
    {
      m_lte_socket = Socket::CreateSocket (GetNode (), m_tid);
      if (Inet6SocketAddress::IsMatchingType (m_lte_peer))
        {
          m_lte_socket->Bind6 ();
        }
      else if (InetSocketAddress::IsMatchingType (m_lte_peer) ||
               PacketSocketAddress::IsMatchingType (m_lte_peer))
        {
          m_lte_socket->Bind ();
        }
      m_lte_socket->Connect (m_lte_peer);
      m_lte_socket->SetAllowBroadcast (true);
      m_lte_socket->ShutdownRecv ();

      m_lte_socket->SetConnectCallback (
        MakeCallback (&MyOnOffApplication::ConnectionSucceeded, this),
        MakeCallback (&MyOnOffApplication::ConnectionFailed, this));
    }

    if (!m_wifi_socket)
    {
      m_wifi_socket = Socket::CreateSocket (GetNode (), m_tid);
      if (Inet6SocketAddress::IsMatchingType (m_wifi_peer))
        {
          m_wifi_socket->Bind6 ();
        }
      else if (InetSocketAddress::IsMatchingType (m_wifi_peer) ||
               PacketSocketAddress::IsMatchingType (m_wifi_peer))
        {
          m_wifi_socket->Bind ();
        }
      m_wifi_socket->Connect (m_wifi_peer);
      m_wifi_socket->SetAllowBroadcast (true);
      m_wifi_socket->ShutdownRecv ();

      m_wifi_socket->SetConnectCallback (
        MakeCallback (&MyOnOffApplication::ConnectionSucceeded, this),
        MakeCallback (&MyOnOffApplication::ConnectionFailed, this));
    }
  m_cbrRateFailSafe = m_cbrRate;

  // Insure no pending event
  CancelEvents ();

  //default using lte
  m_current_on = 0;
  m_current_peer = m_lte_peer;
  m_current_socket = m_lte_socket;
  // If we are not yet connected, there is nothing to do here
  // The ConnectionComplete upcall will start timers at that time
  //if (!m_connected) return;
  //ScheduleStartEvent ();
  StartSending();
}

void MyOnOffApplication::StopApplication () // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);

  CancelEvents ();
  if(m_lte_socket != 0)
    {
      m_lte_socket->Close ();
    }
  else
    {
      NS_LOG_WARN ("OnOffApplication found null lte socket to close in StopApplication");
    }

  if(m_wifi_socket != 0)
    {
      m_wifi_socket->Close ();
    }
  else
    {
      NS_LOG_WARN ("OnOffApplication found null wifi socket to close in StopApplication");
    }
}


void MyOnOffApplication::CancelEvents ()
{
  NS_LOG_FUNCTION (this);

  if (m_sendEvent.IsRunning () && m_cbrRateFailSafe == m_cbrRate )
    { // Cancel the pending send packet event
      // Calculate residual bits since last packet sent
      Time delta (Simulator::Now () - m_lastStartTime);
      int64x64_t bits = delta.To (Time::S) * m_cbrRate.GetBitRate ();
      if(m_current_on==0)
        m_lte_residualBits += bits.GetHigh ();
      else
        m_wifi_residualBits += bits.GetHigh ();
    }
  m_cbrRateFailSafe = m_cbrRate;
  Simulator::Cancel (m_sendEvent);
  //Simulator::Cancel (m_startStopEvent);
}

// Event handlers
void MyOnOffApplication::StartSending ()
{
  NS_LOG_FUNCTION (this);
  m_lastStartTime = Simulator::Now ();
  ScheduleNextTx ();  // Schedule the send packet event
  //ScheduleStopEvent ();
}

void MyOnOffApplication::StopSending ()
{
  NS_LOG_FUNCTION (this);
  CancelEvents ();
  //ScheduleStartEvent ();
}

// Private helpers
void MyOnOffApplication::ScheduleNextTx ()
{
  NS_LOG_FUNCTION (this);

  if (m_maxBytes == 0 || m_totBytes < m_maxBytes)
    {
      uint32_t bits;
      if(m_current_on == 0)
        bits = m_pktSize * 8 - m_lte_residualBits;
      else
        bits = m_pktSize * 8 - m_wifi_residualBits;
      NS_LOG_LOGIC ("bits = " << bits);
      Time nextTime (Seconds (bits /
                              static_cast<double>(m_cbrRate.GetBitRate ()))); // Time till next packet
      NS_LOG_LOGIC ("nextTime = " << nextTime);
      m_sendEvent = Simulator::Schedule (nextTime,
                                         &MyOnOffApplication::SendPacket, this);
    }
  else
    { // All done, cancel any pending events
      StopApplication ();
    }
}

/*
void OnOffApplication::ScheduleStartEvent ()
{  // Schedules the event to start sending data (switch to the "On" state)
  NS_LOG_FUNCTION (this);

  Time offInterval = Seconds (m_offTime->GetValue ());
  NS_LOG_LOGIC ("start at " << offInterval);
  m_startStopEvent = Simulator::Schedule (offInterval, &OnOffApplication::StartSending, this);
}

void OnOffApplication::ScheduleStopEvent ()
{  // Schedules the event to stop sending data (switch to "Off" state)
  NS_LOG_FUNCTION (this);

  Time onInterval = Seconds (m_onTime->GetValue ());
  NS_LOG_LOGIC ("stop at " << onInterval);
  m_startStopEvent = Simulator::Schedule (onInterval, &OnOffApplication::StopSending, this);
}//*/


void MyOnOffApplication::SendPacket ()
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT (m_sendEvent.IsExpired ());
  Ptr<Packet> packet = Create<Packet> (m_pktSize);
  m_txTrace (packet);
  m_current_socket->Send(packet);
  m_totBytes += m_pktSize;
  if (InetSocketAddress::IsMatchingType (m_current_peer))
    {
      NS_LOG_INFO ("At time " << Simulator::Now ().GetSeconds ()
                   << "s on-off application sent "
                   <<  packet->GetSize () << " bytes to "
                   << InetSocketAddress::ConvertFrom(m_current_peer).GetIpv4 ()
                   << " port " << InetSocketAddress::ConvertFrom (m_current_peer).GetPort ()
                   << " total Tx " << m_totBytes << " bytes");
    }
  else if (Inet6SocketAddress::IsMatchingType (m_current_peer))
    {
      NS_LOG_INFO ("At time " << Simulator::Now ().GetSeconds ()
                   << "s on-off application sent "
                   <<  packet->GetSize () << " bytes to "
                   << Inet6SocketAddress::ConvertFrom(m_current_peer).GetIpv6 ()
                   << " port " << Inet6SocketAddress::ConvertFrom (m_current_peer).GetPort ()
                   << " total Tx " << m_totBytes << " bytes");
    }
  m_lastStartTime = Simulator::Now ();
  if(m_current_on == 0)
    m_lte_residualBits = 0;
  else
    m_wifi_residualBits = 0;
  ScheduleNextTx ();
}

int MyOnOffApplication::SwitchNetwork()
{
  std::cout<<"change"<<std::endl;
  StopSending();//first stop current sending
  if(m_current_on ==0 )
    {
      m_current_on = 1;
      m_current_socket = m_wifi_socket;
      m_current_peer= m_wifi_peer;
   }
  else
   {
      m_current_on = 0;
      m_current_socket = m_lte_socket;
      m_current_peer= m_lte_peer;
   }
   StartSending();
   return m_current_on;
}

void MyOnOffApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  
  m_lte_connected = true;
  m_wifi_connected = true;
}

void MyOnOffApplication::ConnectionFailed (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}


} // Namespace ns3
