#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"
#include "ns3/mobility-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("RTP_Simulation");

void TxTrace(std::string context, Ptr<const Packet> packet) {
    std::cout << "TX: " << context << " " << *packet << std::endl;
}

int main(int argc, char *argv[]) {
    LogComponentEnable("UdpClient", LOG_LEVEL_INFO);
    LogComponentEnable("UdpServer", LOG_LEVEL_INFO);

    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices = pointToPoint.Install(nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    uint16_t port = 5000;
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(nodes.Get(1));
    serverApp.Start(Seconds(1.0));
    serverApp.Stop(Seconds(10.0));

    UdpClientHelper client(interfaces.GetAddress(1), port);
    client.SetAttribute("MaxPackets", UintegerValue(1000));
    client.SetAttribute("Interval", TimeValue(MilliSeconds(20)));
    client.SetAttribute("PacketSize", UintegerValue(160));

    ApplicationContainer clientApp = client.Install(nodes.Get(0));
    clientApp.Start(Seconds(2.0));
    clientApp.Stop(Seconds(10.0));

    pointToPoint.EnablePcapAll("rtp_simulation");

    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpClient/Tx", MakeCallback(&TxTrace));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    AnimationInterface anim("rtp_simulation.xml");
    anim.SetConstantPosition(nodes.Get(0), 10, 20);
    anim.SetConstantPosition(nodes.Get(1), 50, 20);
    anim.EnablePacketMetadata(true);

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();

    monitor->SerializeToXmlFile("rtp_flowmonitor.xml", true, true);

    Simulator::Destroy();
    return 0;
}
