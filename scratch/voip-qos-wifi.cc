/* voip-wifi-qos-comparison.cc
 * This program compares VoIP performance under EDCA and WMM QoS settings
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/csma-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("VoipWifiQosComparison");

void RunSimulation(std::string qosType) {
    // Simulation parameters
    double simulationTime = 100.0;
    uint32_t nWifiNodes = 3;
    double packetInterval = 50.0;
    uint32_t packetSize = 1000;

    // Create nodes
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(nWifiNodes);
    NodeContainer wifiApNode;
    wifiApNode.Create(1);
    NodeContainer serverNode;
    serverNode.Create(1);
    NodeContainer csmaNodes;
    csmaNodes.Add(wifiApNode);
    csmaNodes.Add(serverNode);

    // Mobility
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));  // AP
    positionAlloc->Add(Vector(30.0, 0.0, 0.0)); // Server
    
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode);
    mobility.Install(serverNode);

    // STA positions
    MobilityHelper mobilitySta;
    Ptr<ListPositionAllocator> staPositions = CreateObject<ListPositionAllocator>();
    double radius = 40.0;
    for (uint32_t i = 0; i < nWifiNodes; ++i) {
        double angle = i * 2 * M_PI / nWifiNodes;
        staPositions->Add(Vector(radius * cos(angle), radius * sin(angle), 0.0));
    }
    mobilitySta.SetPositionAllocator(staPositions);
    mobilitySta.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                               "Mode", StringValue("Time"),
                               "Time", StringValue("5s"),
                               "Speed", StringValue("ns3::ConstantRandomVariable[Constant=0.5]"),
                               "Bounds", RectangleValue(Rectangle(-100, 100, -100, 100)));
    mobilitySta.Install(wifiStaNodes);

    // WiFi setup
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue("VhtMcs9"),
                               "ControlMode", StringValue("VhtMcs0"));

    // Configure QoS
    if (qosType == "EDCA") {
        Config::SetDefault("ns3::WifiMacQueue::MaxSize", StringValue("500p"));
        Config::SetDefault("ns3::WifiMacQueue::MaxDelay", TimeValue(MilliSeconds(100)));
    } else { // WMM
        wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
        Config::SetDefault("ns3::WifiMacQueue::MaxSize", StringValue("800p"));
        Config::SetDefault("ns3::WifiMacQueue::MaxDelay", TimeValue(MilliSeconds(50)));
    }

    Ssid ssid = Ssid("voip-qos-ns3");
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid), "EnableBeaconJitter", BooleanValue(false));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiApNode);

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, wifiStaNodes);

    // CSMA connection
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));
    NetDeviceContainer csmaDevices = csma.Install(csmaNodes);

    // Internet stack
    InternetStackHelper stack;
    stack.Install(wifiStaNodes);
    stack.Install(wifiApNode);
    stack.Install(serverNode);

    // IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer wifiInterfaces = address.Assign(staDevices);
    address.Assign(apDevice);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer csmaInterfaces = address.Assign(csmaDevices);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Server application
    uint16_t port = 5000;
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(serverNode.Get(0));
    serverApp.Start(Seconds(1.0));
    serverApp.Stop(Seconds(simulationTime));

    // Client applications
    ApplicationContainer allClientApps;
    for (uint32_t i = 0; i < nWifiNodes; i++) {
        double adjustedInterval = packetInterval + (i * 10);
        UdpClientHelper client(csmaInterfaces.GetAddress(1), port);
        client.SetAttribute("MaxPackets", UintegerValue(0));
        client.SetAttribute("Interval", TimeValue(MilliSeconds(adjustedInterval)));
        client.SetAttribute("PacketSize", UintegerValue(packetSize));

        ApplicationContainer clientApp = client.Install(wifiStaNodes.Get(i));
        clientApp.Start(Seconds(5.0 + i * 3.0));
        clientApp.Stop(Seconds(simulationTime));
        allClientApps.Add(clientApp);
    }

    // Return traffic
    for (uint32_t i = 0; i < nWifiNodes; i++) {
        uint16_t returnPort = 6000 + i;
        UdpServerHelper returnServer(returnPort);
        ApplicationContainer returnServerApp = returnServer.Install(wifiStaNodes.Get(i));
        returnServerApp.Start(Seconds(2.0));
        returnServerApp.Stop(Seconds(simulationTime));

        UdpClientHelper returnClient(wifiInterfaces.GetAddress(i), returnPort);
        returnClient.SetAttribute("MaxPackets", UintegerValue(0));
        returnClient.SetAttribute("Interval", TimeValue(MilliSeconds(packetInterval + 20)));
        returnClient.SetAttribute("PacketSize", UintegerValue(packetSize));

        ApplicationContainer returnClientApp = returnClient.Install(serverNode.Get(0));
        returnClientApp.Start(Seconds(10.0 + i * 2.0));
        returnClientApp.Stop(Seconds(simulationTime));
        allClientApps.Add(returnClientApp);
    }

    // Flow Monitor
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    // NetAnim configuration
    std::string animFile = "voip-wifi-qos-" + qosType + ".xml";
    AnimationInterface anim(animFile);
    anim.EnablePacketMetadata(true);

    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i) {
        anim.UpdateNodeSize(i, 10.0, 10.0);
        anim.UpdateNodeColor(wifiStaNodes.Get(i), 255, 0, 0);
    }
    anim.UpdateNodeSize(wifiStaNodes.GetN(), 15.0, 15.0);
    anim.UpdateNodeColor(wifiApNode.Get(0), 0, 255, 0);
    anim.UpdateNodeSize(wifiStaNodes.GetN() + 1, 15.0, 15.0);
    anim.UpdateNodeColor(serverNode.Get(0), 0, 0, 255);

    // Run simulation
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // Calculate statistics
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    double avgThroughput = 0.0;
    double avgDelay = 0.0;
    double avgJitter = 0.0;
    uint32_t lostPackets = 0;
    uint32_t txPackets = 0;
    uint32_t rxPackets = 0;

    std::cout << "\nFlow Information (" << qosType << "):" << std::endl;
    for (auto& stat : stats) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(stat.first);
        std::cout << "Flow " << stat.first << " (" << t.sourceAddress << ":" << t.sourcePort
                  << " -> " << t.destinationAddress << ":" << t.destinationPort << ")" << std::endl;

        txPackets += stat.second.txPackets;
        rxPackets += stat.second.rxPackets;
        lostPackets += stat.second.lostPackets;

        avgThroughput += stat.second.rxBytes * 8.0 / (simulationTime * 1000000.0);
        avgDelay += stat.second.rxPackets > 0 ? stat.second.delaySum.GetMilliSeconds() / stat.second.rxPackets : 0.0;
        avgJitter += (stat.second.rxPackets > 1) ? stat.second.jitterSum.GetMilliSeconds() / (stat.second.rxPackets - 1) : 0.0;
    }

    if (!stats.empty()) {
        avgThroughput /= stats.size();
        avgDelay /= stats.size();
        avgJitter /= stats.size();
    }

    double packetLossRate = (txPackets > 0) ? 100.0 * lostPackets / txPackets : 0.0;

    std::cout << "=== QoS Type: " << qosType << " ===" << std::endl;
    std::cout << "  Average Throughput: " << avgThroughput << " Mbps" << std::endl;
    std::cout << "  Average Delay: " << avgDelay << " ms" << std::endl;
    std::cout << "  Average Jitter: " << avgJitter << " ms" << std::endl;
    std::cout << "  Packet Loss Rate: " << packetLossRate << "%" << std::endl;
    std::cout << "  Total Tx Packets: " << txPackets << std::endl;
    std::cout << "  Total Rx Packets: " << rxPackets << std::endl;
    std::cout << "  Total Lost Packets: " << lostPackets << std::endl;

    Simulator::Destroy();
}

int main(int argc, char *argv[]) {
    // Run simulation for both QoS types
    std::cout << "Running VoIP WiFi QoS comparison simulation...\n";
    
    std::cout << "\n=== EDCA Simulation ===\n";
    RunSimulation("EDCA");
    
    std::cout << "\n=== WMM Simulation ===\n";
    RunSimulation("WMM");

    std::cout << "\n\n*****************************************************************" << std::endl;
    std::cout << "NetAnim files created:" << std::endl;
    std::cout << "- voip-wifi-qos-EDCA.xml" << std::endl;
    std::cout << "- voip-wifi-qos-WMM.xml" << std::endl;
    std::cout << "Load these files in NetAnim to visualize the different QoS scenarios" << std::endl;
    std::cout << "*****************************************************************" << std::endl;

    return 0;
}
