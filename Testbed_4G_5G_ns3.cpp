/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Project: LTE vs 5G NR Comparative Testbed for IoT Communications
 * Course:  Data Communication, ETTI UPB (2025-2026)
 * Author:  Maria-Roxana Paraschiv, Andrei Mocanu, Tudor Marcu
 * Coordinator: Prof. Dr. Ing. Seyedsalar Sefati
 * 
 * Features:
 * - Dual radio support: 4G LTE (LENA) & 5G NR (ns-3-nr module)
 * - Mixed IoT traffic classes: Downlink Sensors (CBR UDP) & Uplink Video (ON/OFF UDP)
 * - Multi-cell topology with A3 RSRP handover via X2 interface
 * - FlowMonitor flow-level QoS metrics extraction (Latency, PDR, Goodput, Loss)
 * - Designed for discrete-event network simulation on Linux
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/config-store-module.h"
#include "ns3/lte-module.h"
#include "ns3/nr-module.h"
#include "ns3/antenna-module.h"

#include <fstream>
#include <iomanip>
#include <filesystem>

using namespace ns3;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("Project6");

// Global results directory
static std::string g_resultsDir = "results_project6";

void EnsureDir(const std::string& path)
{
    if (!fs::exists(path))
    {
        fs::create_directories(path);
    }
}

// Structure for QoS statistics collection
struct QosStats
{
    double sensorLatencyMs = 0;
    double sensorPdr = 0;
    double videoGoodputMbps = 0;
    double videoLossPct = 0;
    uint64_t sensorTxPkts = 0;
    uint64_t sensorRxPkts = 0;
    uint64_t videoTxPkts = 0;
    uint64_t videoRxPkts = 0;
};

// Extract QoS metrics via FlowMonitor
QosStats ComputeQos(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier,
                    uint16_t sensorPort, uint32_t nSensors,
                    uint16_t videoPort, uint32_t nVideos, double simTime)
{
    QosStats stats;
    double sensorDelaySum = 0;
    double videoBytes = 0;

    monitor->CheckForLostPackets();
    auto flowStats = monitor->GetFlowStats();

    for (auto& flow : flowStats)
    {
        auto tuple = classifier->FindFlow(flow.first);
        uint16_t port = tuple.destinationPort;

        bool isSensor = (port >= sensorPort && port < sensorPort + nSensors);
        bool isVideo = (port >= videoPort && port < videoPort + nVideos);

        if (isSensor)
        {
            stats.sensorTxPkts += flow.second.txPackets;
            stats.sensorRxPkts += flow.second.rxPackets;
            sensorDelaySum += flow.second.delaySum.GetMilliSeconds();
        }
        else if (isVideo)
        {
            stats.videoTxPkts += flow.second.txPackets;
            stats.videoRxPkts += flow.second.rxPackets;
            videoBytes += flow.second.rxBytes;
        }
    }

    if (stats.sensorRxPkts > 0)
    {
        stats.sensorLatencyMs = sensorDelaySum / stats.sensorRxPkts;
    }
    if (stats.sensorTxPkts > 0)
    {
        stats.sensorPdr = 100.0 * stats.sensorRxPkts / stats.sensorTxPkts;
    }
    if (stats.videoTxPkts > 0)
    {
        stats.videoLossPct = 100.0 * (stats.videoTxPkts - stats.videoRxPkts) / stats.videoTxPkts;
    }
    stats.videoGoodputMbps = (videoBytes * 8.0) / (simTime * 1e6);

    return stats;
}

int main(int argc, char* argv[])
{
    // Simulation parameters
    bool useNr = false;          // false = LTE, true = 5G NR
    double simTime = 30.0;
    uint32_t nSensors = 20;
    uint32_t nVideos = 3;
    uint32_t nEnbs = 2;
    uint32_t bwRb = 50;          // LTE bandwidth in RBs (50 RBs = 10 MHz)
    uint32_t nrBwMhz = 40;       // 5G NR bandwidth in MHz
    uint32_t numerology = 0;
    std::string scheduler = "PF";
    double txPower = 30.0;       // dBm
    double distance = 500.0;     // Inter-BS distance in meters

    CommandLine cmd;
    cmd.AddValue("useNr", "Use NR (true) or LTE (false)", useNr);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("nSensors", "Number of sensor UEs", nSensors);
    cmd.AddValue("nVideos", "Number of video UEs", nVideos);
    cmd.AddValue("nEnbs", "Number of eNBs / gNBs", nEnbs);
    cmd.AddValue("bwRb", "LTE bandwidth in RBs", bwRb);
    cmd.AddValue("nrBwMhz", "NR bandwidth in MHz", nrBwMhz);
    cmd.AddValue("numerology", "5G NR numerology (0 or 1)", numerology);
    cmd.AddValue("scheduler", "Scheduler type (PF or RR)", scheduler);
    cmd.AddValue("txPower", "Transmit power in dBm", txPower);
    cmd.Parse(argc, argv);

    // Results logging directory setup
    EnsureDir(g_resultsDir);
    std::string runId = std::to_string(std::time(nullptr));
    std::string runDir = g_resultsDir + "/run_" + runId;
    EnsureDir(runDir);

    NS_LOG_UNCOND("==================================================");
    NS_LOG_UNCOND("Project 6: LTE vs 5G NR Comparative IoT Testbed");
    NS_LOG_UNCOND("Technology: " << (useNr ? "5G NR" : "4G LTE"));
    NS_LOG_UNCOND("Sensors: " << nSensors << " | Video UEs: " << nVideos);
    NS_LOG_UNCOND("==================================================");

    Ptr<Node> pgw;
    NetDeviceContainer enbDevs, sensorDevs, videoDevs;
    Ipv4Address remoteHostAddr;
    Ptr<LteHelper> lteHelper;
    Ptr<NrHelper> nrHelper;

    if (useNr)
    {
        // --------------------------------------------------------
        // 5G NR CONFIGURATION (ns-3-nr module)
        // --------------------------------------------------------
        NS_LOG_UNCOND("Setting up 5G NR network stack...");

        Ptr<NrPointToPointEpcHelper> nrEpcHelper = CreateObject<NrPointToPointEpcHelper>();
        Ptr<IdealBeamformingHelper> beamformingHelper = CreateObject<IdealBeamformingHelper>();
        nrHelper = CreateObject<NrHelper>();
        nrHelper->SetBeamformingHelper(beamformingHelper);
        nrHelper->SetEpcHelper(nrEpcHelper);

        double centralFreq = 3.5e9; // 3.5 GHz
        double bandwidth = nrBwMhz * 1e6;

        CcBwpCreator ccBwpCreator;
        CcBwpCreator::SimpleOperationBandConf bandConf(centralFreq, bandwidth, 1, BandwidthPartInfo::UMa);
        OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);

        Config::SetDefault("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue(MilliSeconds(0)));
        nrHelper->SetChannelConditionModelAttribute("UpdatePeriod", TimeValue(MilliSeconds(0)));
        nrHelper->SetPathlossAttribute("ShadowingEnabled", BooleanValue(false));
        nrHelper->InitializeOperationBand(&band);
        BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

        beamformingHelper->SetAttribute("BeamformingMethod", TypeIdValue(DirectPathBeamforming::GetTypeId()));

        // Multi-antenna array configuration
        nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(2));
        nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(2));
        nrHelper->SetGnbAntennaAttribute("AntennaElement", PointerValue(CreateObject<IsotropicAntennaModel>()));
        nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(1));
        nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(1));
        nrHelper->SetUeAntennaAttribute("AntennaElement", PointerValue(CreateObject<IsotropicAntennaModel>()));

        pgw = nrEpcHelper->GetPgwNode();

        // Remote host application server
        NodeContainer remoteHostContainer;
        remoteHostContainer.Create(1);
        Ptr<Node> remoteHost = remoteHostContainer.Get(0);
        InternetStackHelper internet;
        internet.Install(remoteHostContainer);

        // Core network Point-to-Point link (PGW <-> Remote Host)
        PointToPointHelper p2ph;
        p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gbps")));
        p2ph.SetDeviceAttribute("Mtu", UintegerValue(2500));
        p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(5)));
        NetDeviceContainer p2pDevices = p2ph.Install(pgw, remoteHost);

        Ipv4AddressHelper ipv4h;
        ipv4h.SetBase("1.0.0.0", "255.0.0.0");
        Ipv4InterfaceContainer internetIfaces = ipv4h.Assign(p2pDevices);
        remoteHostAddr = internetIfaces.GetAddress(1);

        Ipv4StaticRoutingHelper routingHelper;
        Ptr<Ipv4StaticRouting> remoteRouting = routingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
        remoteRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

        // Node creation: gNBs and UEs
        NodeContainer gnbNodes, sensorNodes, videoNodes;
        gnbNodes.Create(nEnbs);
        sensorNodes.Create(nSensors);
        videoNodes.Create(nVideos);

        // Base station positioning
        MobilityHelper gnbMobility;
        Ptr<ListPositionAllocator> gnbPos = CreateObject<ListPositionAllocator>();
        for (uint32_t i = 0; i < nEnbs; ++i)
        {
            gnbPos->Add(Vector(distance * (i + 1), distance, 30.0));
        }
        gnbMobility.SetPositionAllocator(gnbPos);
        gnbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        gnbMobility.Install(gnbNodes);

        // UE Random Walk Mobility
        MobilityHelper ueMobility;
        ueMobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
            "X", StringValue("ns3::UniformRandomVariable[Min=300.0|Max=1200.0]"),
            "Y", StringValue("ns3::UniformRandomVariable[Min=300.0|Max=700.0]"));
        ueMobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
            "Bounds", RectangleValue(Rectangle(300, 1200, 300, 700)),
            "Speed", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
        ueMobility.Install(sensorNodes);
        ueMobility.Install(videoNodes);

        // Install 5G NetDevices
        enbDevs = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
        sensorDevs = nrHelper->InstallUeDevice(sensorNodes, allBwps);
        videoDevs = nrHelper->InstallUeDevice(videoNodes, allBwps);

        // Set TX power and numerology
        for (auto it = enbDevs.Begin(); it != enbDevs.End(); ++it)
        {
            Ptr<NrGnbNetDevice> gnb = DynamicCast<NrGnbNetDevice>(*it);
            nrHelper->GetGnbPhy(gnb, 0)->SetAttribute("Numerology", UintegerValue(numerology));
            nrHelper->GetGnbPhy(gnb, 0)->SetAttribute("TxPower", DoubleValue(txPower));
            gnb->UpdateConfig();
        }
        for (auto it = sensorDevs.Begin(); it != sensorDevs.End(); ++it)
        {
            Ptr<NrUeNetDevice> ue = DynamicCast<NrUeNetDevice>(*it);
            nrHelper->GetUePhy(ue, 0)->SetAttribute("TxPower", DoubleValue(txPower));
            ue->UpdateConfig();
        }
        for (auto it = videoDevs.Begin(); it != videoDevs.End(); ++it)
        {
            Ptr<NrUeNetDevice> ue = DynamicCast<NrUeNetDevice>(*it);
            nrHelper->GetUePhy(ue, 0)->SetAttribute("TxPower", DoubleValue(txPower));
            ue->UpdateConfig();
        }

        internet.Install(sensorNodes);
        internet.Install(videoNodes);

        Ipv4InterfaceContainer sensorIfaces = nrEpcHelper->AssignUeIpv4Address(sensorDevs);
        Ipv4InterfaceContainer videoIfaces = nrEpcHelper->AssignUeIpv4Address(videoDevs);

        Ipv4Address gateway = nrEpcHelper->GetUeDefaultGatewayAddress();
        for (uint32_t i = 0; i < sensorNodes.GetN(); ++i)
        {
            Ptr<Ipv4StaticRouting> ueRouting = routingHelper.GetStaticRouting(sensorNodes.Get(i)->GetObject<Ipv4>());
            ueRouting->SetDefaultRoute(gateway, 1);
        }
        for (uint32_t i = 0; i < videoNodes.GetN(); ++i)
        {
            Ptr<Ipv4StaticRouting> ueRouting = routingHelper.GetStaticRouting(videoNodes.Get(i)->GetObject<Ipv4>());
            ueRouting->SetDefaultRoute(gateway, 1);
        }

        nrHelper->AttachToClosestEnb(sensorDevs, enbDevs);
        nrHelper->AttachToClosestEnb(videoDevs, enbDevs);

        // Traffic Applications
        uint16_t sensorPort = 4000;
        uint16_t videoPort = 6000;

        // Sensor Traffic (Downlink UDP CBR)
        for (uint32_t i = 0; i < nSensors; ++i)
        {
            uint16_t port = sensorPort + i;
            Ipv4Address ueAddr = sensorIfaces.GetAddress(i);

            PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer sinkApp = sink.Install(sensorNodes.Get(i));
            sinkApp.Start(Seconds(0.5));
            sinkApp.Stop(Seconds(simTime + 1));

            OnOffHelper source("ns3::UdpSocketFactory", InetSocketAddress(ueAddr, port));
            source.SetAttribute("DataRate", StringValue("1kbps"));
            source.SetAttribute("PacketSize", UintegerValue(128));
            source.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
            source.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
            ApplicationContainer srcApp = source.Install(remoteHost);
            srcApp.Start(Seconds(1.0));
            srcApp.Stop(Seconds(simTime));
        }

        // Video Traffic (Uplink UDP ON/OFF)
        for (uint32_t i = 0; i < nVideos; ++i)
        {
            uint16_t port = videoPort + i;
            Ptr<Node> ue = videoNodes.Get(i);

            PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer sinkApp = sink.Install(remoteHost);
            sinkApp.Start(Seconds(0.5));
            sinkApp.Stop(Seconds(simTime + 1));

            OnOffHelper source("ns3::UdpSocketFactory", InetSocketAddress(remoteHostAddr, port));
            source.SetAttribute("DataRate", StringValue("2Mbps"));
            source.SetAttribute("PacketSize", UintegerValue(1200));
            source.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.8]"));
            source.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.2]"));
            ApplicationContainer srcApp = source.Install(ue);
            srcApp.Start(Seconds(1.5));
            srcApp.Stop(Seconds(simTime));
        }

        FlowMonitorHelper flowHelper;
        Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

        Simulator::Stop(Seconds(simTime + 1));
        Simulator::Run();

        Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
        QosStats stats = ComputeQos(monitor, classifier, sensorPort, nSensors, videoPort, nVideos, simTime);

        NS_LOG_UNCOND("\n--- Results ---");
        NS_LOG_UNCOND("Sensor: latency=" << stats.sensorLatencyMs << "ms, PDR=" << stats.sensorPdr << "%");
        NS_LOG_UNCOND("Video: goodput=" << stats.videoGoodputMbps << "Mbps, loss=" << stats.videoLossPct << "%");

        monitor->SerializeToXmlFile(runDir + "/flowmon.xml", true, true);
        Simulator::Destroy();
    }
    else
    {
        // --------------------------------------------------------
        // 4G LTE CONFIGURATION (LENA module)
        // --------------------------------------------------------
        NS_LOG_UNCOND("Setting up LTE network...");

        lteHelper = CreateObject<LteHelper>();
        Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
        lteHelper->SetEpcHelper(epcHelper);

        if (scheduler == "RR")
        {
            lteHelper->SetSchedulerType("ns3::RrFfMacScheduler");
        }
        else
        {
            lteHelper->SetSchedulerType("ns3::PfFfMacScheduler");
        }

        // A3 RSRP Handover Algorithm configuration
        lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
        lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(2.0));
        lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(120)));

        pgw = epcHelper->GetPgwNode();

        NodeContainer remoteHostContainer;
        remoteHostContainer.Create(1);
        Ptr<Node> remoteHost = remoteHostContainer.Get(0);
        InternetStackHelper internet;
        internet.Install(remoteHostContainer);

        PointToPointHelper p2ph;
        p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gbps")));
        p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));
        p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(10)));
        NetDeviceContainer p2pDevices = p2ph.Install(pgw, remoteHost);

        Ipv4AddressHelper ipv4h;
        ipv4h.SetBase("1.0.0.0", "255.0.0.0");
        Ipv4InterfaceContainer internetIfaces = ipv4h.Assign(p2pDevices);
        remoteHostAddr = internetIfaces.GetAddress(1);

        Ipv4StaticRoutingHelper routingHelper;
        Ptr<Ipv4StaticRouting> remoteRouting = routingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
        remoteRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

        NodeContainer enbNodes, sensorNodes, videoNodes;
        enbNodes.Create(nEnbs);
        sensorNodes.Create(nSensors);
        videoNodes.Create(nVideos);

        MobilityHelper enbMobility;
        Ptr<ListPositionAllocator> enbPos = CreateObject<ListPositionAllocator>();
        for (uint32_t i = 0; i < nEnbs; ++i)
        {
            enbPos->Add(Vector(distance * (i + 1), distance, 30.0));
        }
        enbMobility.SetPositionAllocator(enbPos);
        enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        enbMobility.Install(enbNodes);

        MobilityHelper ueMobility;
        ueMobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
            "X", StringValue("ns3::UniformRandomVariable[Min=300.0|Max=1200.0]"),
            "Y", StringValue("ns3::UniformRandomVariable[Min=300.0|Max=700.0]"));
        ueMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        ueMobility.Install(sensorNodes);
        ueMobility.Install(videoNodes);

        Config::SetDefault("ns3::LteEnbPhy::TxPower", DoubleValue(txPower));
        lteHelper->SetEnbDeviceAttribute("DlBandwidth", UintegerValue(bwRb));
        lteHelper->SetEnbDeviceAttribute("UlBandwidth", UintegerValue(bwRb));

        enbDevs = lteHelper->InstallEnbDevice(enbNodes);
        sensorDevs = lteHelper->InstallUeDevice(sensorNodes);
        videoDevs = lteHelper->InstallUeDevice(videoNodes);

        internet.Install(sensorNodes);
        internet.Install(videoNodes);

        Ipv4InterfaceContainer sensorIfaces = epcHelper->AssignUeIpv4Address(sensorDevs);
        Ipv4InterfaceContainer videoIfaces = epcHelper->AssignUeIpv4Address(videoDevs);

        Ipv4Address gateway = epcHelper->GetUeDefaultGatewayAddress();
        for (uint32_t i = 0; i < sensorNodes.GetN(); ++i)
        {
            Ptr<Ipv4StaticRouting> ueRouting = routingHelper.GetStaticRouting(sensorNodes.Get(i)->GetObject<Ipv4>());
            ueRouting->SetDefaultRoute(gateway, 1);
        }
        for (uint32_t i = 0; i < videoNodes.GetN(); ++i)
        {
            Ptr<Ipv4StaticRouting> ueRouting = routingHelper.GetStaticRouting(videoNodes.Get(i)->GetObject<Ipv4>());
            ueRouting->SetDefaultRoute(gateway, 1);
        }

        for (uint32_t i = 0; i < sensorDevs.GetN(); ++i)
        {
            lteHelper->Attach(sensorDevs.Get(i), enbDevs.Get(i % nEnbs));
        }
        for (uint32_t i = 0; i < videoDevs.GetN(); ++i)
        {
            lteHelper->Attach(videoDevs.Get(i), enbDevs.Get(i % nEnbs));
        }

        lteHelper->AddX2Interface(enbNodes);

        uint16_t sensorPort = 4000;
        uint16_t videoPort = 6000;

        for (uint32_t i = 0; i < nSensors; ++i)
        {
            uint16_t port = sensorPort + i;
            Ptr<Node> ue = sensorNodes.Get(i);
            Ipv4Address ueAddr = sensorIfaces.GetAddress(i);

            PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer sinkApp = sink.Install(ue);
            sinkApp.Start(Seconds(0.5));
            sinkApp.Stop(Seconds(simTime + 1));

            OnOffHelper source("ns3::UdpSocketFactory", InetSocketAddress(ueAddr, port));
            source.SetAttribute("DataRate", StringValue("1kbps"));
            source.SetAttribute("PacketSize", UintegerValue(128));
            source.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
            source.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
            ApplicationContainer srcApp = source.Install(remoteHost);
            srcApp.Start(Seconds(1.0));
            srcApp.Stop(Seconds(simTime));
        }

        for (uint32_t i = 0; i < nVideos; ++i)
        {
            uint16_t port = videoPort + i;
            Ptr<Node> ue = videoNodes.Get(i);

            PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer sinkApp = sink.Install(remoteHost);
            sinkApp.Start(Seconds(0.5));
            sinkApp.Stop(Seconds(simTime + 1));

            OnOffHelper source("ns3::UdpSocketFactory", InetSocketAddress(remoteHostAddr, port));
            source.SetAttribute("DataRate", StringValue("2Mbps"));
            source.SetAttribute("PacketSize", UintegerValue(1200));
            source.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.8]"));
            source.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.2]"));
            ApplicationContainer srcApp = source.Install(ue);
            srcApp.Start(Seconds(1.5));
            srcApp.Stop(Seconds(simTime));
        }

        FlowMonitorHelper flowHelper;
        Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

        Simulator::Stop(Seconds(simTime + 1));
        Simulator::Run();

        Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
        QosStats stats = ComputeQos(monitor, classifier, sensorPort, nSensors, videoPort, nVideos, simTime);

        NS_LOG_UNCOND("\n--- Results ---");
        NS_LOG_UNCOND("Sensor: latency=" << stats.sensorLatencyMs << "ms, PDR=" << stats.sensorPdr << "%");
        NS_LOG_UNCOND("Video: goodput=" << stats.videoGoodputMbps << "Mbps, loss=" << stats.videoLossPct << "%");

        monitor->SerializeToXmlFile(runDir + "/flowmon.xml", true, true);

        std::ofstream summary(runDir + "/summary.txt");
        summary << "Technology: LTE\n";
        summary << "Sensors: " << nSensors << "\n";
        summary << "Videos: " << nVideos << "\n";
        summary << "Bandwidth: " << bwRb << " RBs\n";
        summary << "Sensor Latency: " << stats.sensorLatencyMs << " ms\n";
        summary << "Sensor PDR: " << stats.sensorPdr << " %\n";
        summary << "Video Goodput: " << stats.videoGoodputMbps << " Mbps\n";
        summary << "Video Loss: " << stats.videoLossPct << " %\n";
        summary.close();

        Simulator::Destroy();
    }

    NS_LOG_UNCOND("\nSimulation complete. Results in: " << runDir);
    return 0;
}