#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/wifi-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/fd-net-device-module.h"
#include "ns3/config-store-module.h"
#include "ns3/point-to-point-module.h"

#include "signal.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("WifiCooja");

void
signalHandler (int signo)
{
  if (signo == SIGINT)
    {
      Simulator::Stop();
      std::cout.flush ();
      std::cerr.flush ();
      exit (0);
    }
}


void rxCallback (Ptr< const Packet > packet, const Address &address)
{
  Ipv6Header header;
  int s = packet->PeekHeader (header);
  std::cout <<s << " " << header.GetSourceAddress () << " " << address << "\n";
}


int main (int argc, char **argv){

  unsigned int interPacketInterval = 1000;
  int noSTA = 5;
  int noAP = 10;
  uint32_t noCooja = 1;
  std::string coojaConfig("ns3CoojaWifi");
  int seed = 1;

  if (signal (SIGINT,signalHandler) == SIG_ERR)
    {
      std::cerr << "Signal register error\n";
    }

  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::RealtimeSimulatorImpl::SynchronizationMode", StringValue ("HardLimit"));
  Config::SetDefault ("ns3::RealtimeSimulatorImpl::HardLimit", TimeValue (Time ("500ms")));
  
  CommandLine cmd;
  cmd.AddValue ("interPacketInterval","Inter packet interval [ms]",interPacketInterval);
  cmd.AddValue ("noAP","Number of wifi access points",noAP);
  cmd.AddValue ("noSTA","Number of wifi stations per AP",noSTA);
  cmd.AddValue ("noCooja","Number of cooja connections",noCooja);
  cmd.AddValue ("CoojaConfig","Cooja integration configuration file",coojaConfig);
  cmd.AddValue ("seed","Random seed",seed);
  cmd.Parse (argc, argv);
  
  RngSeedManager::SetSeed (seed);

  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults ();
  
  
  NodeContainer remote;
  NodeContainer aps;
  NodeContainer stas;
  
  remote.Create (2);
  Ptr<Node> remoteHost = remote.Get (0);
  Ptr<Node> gw = remote.Get (1);
  
  aps.Create (noAP);
  
  
  std::vector<NodeContainer> stationsPerAP;
  for (int i = 0; i < noAP; i++)
    {
      NodeContainer apStas;
      apStas.Create (noSTA);
      stas.Add (apStas);
      stationsPerAP.push_back (apStas);
    }
  
  NodeContainer all = NodeContainer::GetGlobal();
  NodeContainer csmaNodes;
  csmaNodes.Add (gw);
  csmaNodes.Add (aps);
  NodeContainer coojaNodes (remoteHost);
  
  InternetStackHelper inetv6;
  inetv6.SetIpv4StackInstall (false);
  inetv6.Install (all);

  CoojaFdNetDevicesHelper coojaHelper (coojaConfig.c_str());

  //Create P2P Channel
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (1500));
  p2ph.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (10)));
  NetDeviceContainer backDevs = p2ph.Install (remoteHost,gw);

  Ipv6AddressHelper ipv6;
  ipv6.SetBase (Ipv6Address ("7777:f11d::"), Ipv6Prefix (64));
  Ipv6InterfaceContainer p2pIfs = ipv6.Assign (backDevs);
  p2pIfs.SetForwarding (0,true);
  p2pIfs.SetForwarding (1,true);
  p2pIfs.SetDefaultRouteInAllNodes (0);
  Ipv6Address gwGwAddr = gw->GetObject<Ipv6> ()->GetAddress (1,0).GetAddress ();
  
  //Create CSMA LAN
  
  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", DataRateValue (5000000));
  csma.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (10)));
  NetDeviceContainer csmaDevs = csma.Install (csmaNodes);
  
  ipv6.SetBase (Ipv6Address ("7777:f00d::"), Ipv6Prefix (64));
  Ipv6InterfaceContainer csmaIfs = ipv6.Assign (csmaDevs);
  //csmaIfs.SetForwarding (0, true);
  for (uint32_t i = 0; i < csmaIfs.GetN (); i++)
    {
      csmaIfs.SetForwarding (i, true);
    }
  
  csmaIfs.SetDefaultRouteInAllNodes (0);
  
  Ipv6StaticRoutingHelper ipv6RoutingHelper;
  Ptr<Ipv6StaticRouting> remoteHostStaticRouting = ipv6RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv6> ());
  Ptr<Ipv6StaticRouting> gwStaticRouting = ipv6RoutingHelper.GetStaticRouting (gw->GetObject<Ipv6> ());
  uint32_t remoteIf = remoteHost->GetObject<Ipv6> ()->GetInterfaceForPrefix (Ipv6Address ("7777:f00d::"), Ipv6Prefix (64));
  uint32_t gwIf = gw->GetObject<Ipv6> ()->GetInterfaceForPrefix (Ipv6Address ("7777:f11d::"), Ipv6Prefix (64));
  
  
  //Create Wifi Networks
  
  WifiHelper wifi;
  MobilityHelper mobility;
  WifiMacHelper wifiMac;
  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
  
  uint32_t c = 1;
  
  for (int i = 0; i < noAP; i++)
    {
      Ptr<Node> ap = aps.Get (i);
      Ptr<YansWifiChannel> channel = wifiChannel.Create();
      wifiPhy.SetChannel (channel);
      std::stringstream ssidNameStream;
      ssidNameStream << "wifi-" << i+1;
      std::string ssidName = ssidNameStream.str();
      Ssid ssid = Ssid (ssidName);
      wifi.SetRemoteStationManager ("ns3::ArfWifiManager");
  
      NetDeviceContainer wifiDevs;
      wifiMac.SetType ("ns3::ApWifiMac",
                       "Ssid", SsidValue (ssid));
      wifiDevs.Add(wifi.Install (wifiPhy, wifiMac, ap));
      wifiMac.SetType ("ns3::StaWifiMac",
                       "ActiveProbing", BooleanValue (true),
                       "Ssid", SsidValue (ssid));
      wifiDevs.Add (wifi.Install (wifiPhy, wifiMac, stationsPerAP [i]));
      std::stringstream ipv6AddrStream;
      ipv6AddrStream << "7777:" << i+1 << "::";
      Ipv6Address wifiNetwork (ipv6AddrStream.str().c_str());
      ipv6.SetBase (wifiNetwork, Ipv6Prefix (64));
      Ipv6InterfaceContainer wifiIfs = ipv6.Assign (wifiDevs);
      wifiIfs.SetForwarding (0, true);
      wifiIfs.SetDefaultRouteInAllNodes (0);
      Ipv6Address wifiGwAddr = ap->GetObject<Ipv6> ()->GetAddress (1,0).GetAddress ();
      remoteHostStaticRouting->AddNetworkRouteTo (wifiNetwork, Ipv6Prefix (64), gwGwAddr,remoteIf);
      gwStaticRouting->AddNetworkRouteTo (wifiNetwork, Ipv6Prefix (64), wifiGwAddr,gwIf);
      Ptr<Ipv6StaticRouting> apStaticRouting = ipv6RoutingHelper.GetStaticRouting (ap->GetObject<Ipv6> ());
      uint32_t wifiIf = ap->GetObject<Ipv6> ()->GetInterfaceForPrefix (wifiNetwork, Ipv6Prefix (64));
      apStaticRouting->AddNetworkRouteTo (wifiNetwork, Ipv6Prefix (64), wifiIf);
      //Add routes for cooja
      for (int j = 0; j < noSTA; j++)
        {
          if (c > noCooja) break; 
          Ptr<Node> sta = stationsPerAP[i].Get (j);
          wifiIfs.SetForwarding (j+1, true);
          Ipv6Address coojaGwAddr = sta->GetObject<Ipv6> ()->GetAddress (1,0).GetAddress ();
          Ipv6Address coojaNetwork = coojaHelper.GetNetwork (c);
          Ipv6Prefix coojaPrefix = coojaHelper.GetPrefix (c);
          remoteHostStaticRouting->AddNetworkRouteTo (coojaNetwork, coojaPrefix, gwGwAddr, remoteIf);
          gwStaticRouting->AddNetworkRouteTo (coojaNetwork, coojaPrefix, wifiGwAddr, gwIf);
          apStaticRouting->AddNetworkRouteTo (coojaNetwork, coojaPrefix, coojaGwAddr, wifiIf);
          coojaNodes.Add (sta);
          c++;
        }
    }
  mobility.Install (aps);
  mobility.Install (stas);
  
  if (noCooja > 0) coojaHelper.CreateGatewayNodes (coojaNodes);
  //Background Traffic
  
  uint32_t dlPort = 1234;
  uint32_t ulPort = 2000;
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable> ();
  
  for (uint32_t i = noCooja; i < stas.GetN(); i++)
      {
        ++ulPort;
        PacketSinkHelper dlPacketSinkHelper ("ns3::UdpSocketFactory", Inet6SocketAddress (Ipv6Address::GetAny (), dlPort));
        PacketSinkHelper ulPacketSinkHelper ("ns3::UdpSocketFactory", Inet6SocketAddress (Ipv6Address::GetAny (), ulPort));
  
        serverApps.Add (dlPacketSinkHelper.Install (stas.Get (i)));
  
        serverApps.Add (ulPacketSinkHelper.Install (remoteHost));
  
        UdpClientHelper dlClient (stas.Get (i)->GetObject<Ipv6> ()->GetAddress (1,1).GetAddress (), dlPort);
        dlClient.SetAttribute ("Interval", TimeValue (MilliSeconds(interPacketInterval)));
        dlClient.SetAttribute ("MaxPackets", UintegerValue (UINT_MAX));
//        dlClient.Install (remoteHost).Start (MilliSeconds (120000 + rand->GetValue(0,interPacketInterval)));
  
        UdpClientHelper ulClient (remoteHost->GetObject<Ipv6> ()->GetAddress (1,1).GetAddress (), ulPort);
        ulClient.SetAttribute ("Interval", TimeValue (MilliSeconds(interPacketInterval)));
        ulClient.SetAttribute ("MaxPackets", UintegerValue (UINT_MAX));
        ulClient.Install (stas.Get (i)).Start (MilliSeconds (120000 + rand->GetValue(0,interPacketInterval)));
       
//        clientApps.Add (dlClient.Install (remoteHost));
//        clientApps.Add (ulClient.Install (stas.Get (i)));
      }
    
  
    serverApps.Start (Seconds (2));
    clientApps.Start (Seconds (2));
  
  
    Simulator::Stop (Seconds (1200));
    Simulator::Run ();
  
}

