using System;
using System.Collections.Generic;
using System.Drawing;
using System.Text;
using System.Threading;
using oVRlay.SimHubLeds;

internal static class LedTests
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static Color[] Colors()
    {
        Color[] colors = new Color[88];
        colors[0] = Color.FromArgb(18, 52, 86);
        colors[1] = Color.FromArgb(255, 0, 128);
        colors[87] = Color.FromArgb(171, 205, 239);
        return colors;
    }

    public static int Main()
    {
        try
        {
            ModelRouting();
            long now = 0;
            var sent = new List<byte[]>();
            var forwarder = new LedForwarder(delegate { return now; }, 9000, sent.Add);
            Check(forwarder.TryPublish(Colors), "First complete LED frame must be emitted.");
            Check(sent.Count == 1 && sent[0].Length == 280, "Frame must be exactly 280 bytes.");
            byte[] packet = sent[0];
            Check(Encoding.ASCII.GetString(packet, 0, 8) == "AGLED001", "Receiver magic is invalid.");
            Check(packet[8] == 1 && packet[9] == 0 && packet[10] == 0 && packet[11] == 0,
                "Sequence must start at one, little endian.");
            Check(packet[12] == 88 && packet[13] == 0 && packet[14] == 0 && packet[15] == 0,
                "LED count must be 88, little endian.");
            Check(packet[16] == 18 && packet[17] == 52 && packet[18] == 86 &&
                packet[19] == 255 && packet[20] == 0 && packet[21] == 128 &&
                packet[277] == 171 && packet[278] == 205 && packet[279] == 239,
                "RGB channels and physical first/last LED positions must be preserved.");

            now = 99;
            Check(!forwarder.TryPublish(delegate { throw new Exception("Must not evaluate throttled colors"); }),
                "Frame before the 90 Hz interval must be skipped.");
            now = 100;
            Check(forwarder.TryPublish(Colors) && sent[1][8] == 2,
                "Next frame at 90 Hz boundary must advance sequence.");
            now = 200;
            Check(!forwarder.TryPublish(delegate { return new Color[87]; }), "Partial frames must be rejected.");
            now = 300;
            Check(!forwarder.TryPublish(delegate { throw new InvalidOperationException("Disconnected"); }),
                "Provider failures must not escape to SimHub.");
            Check(sent.Count == 2, "Invalid colors must never produce a packet.");
            now = 400;
            Check(forwarder.TryPublish(Colors) && sent[2][8] == 3 && forwarder.LastError == null,
                "A provider must recover after a failed frame without a sequence gap.");

            bool fail = true;
            var recovering = new LedForwarder(delegate { return now; }, 9000,
                delegate(byte[] data) { if (fail) throw new InvalidOperationException("Queue closed"); sent.Add(data); });
            Check(!recovering.TryPublish(Colors), "Transport failure must not escape to SimHub.");
            fail = false;
            now += 100;
            Check(recovering.TryPublish(Colors), "Transport must recover on the next frame.");

            now = 500;
            var boundary = new LedForwarder(delegate { return now; }, 9000, sent.Add);
            for (int i = 0; i < 256; ++i) { now += 100; Check(boundary.TryPublish(Colors), "Sequence fixture failed."); }
            packet = sent[sent.Count - 1];
            Check(packet[8] == 0 && packet[9] == 1 && packet[10] == 0 && packet[11] == 0,
                "Sequence must carry into the second little-endian byte.");

            using (var entered = new ManualResetEvent(false))
            using (var release = new ManualResetEvent(false))
            {
                var concurrent = new LedForwarder(delegate { return now; }, 9000, sent.Add);
                var first = new Thread(delegate()
                {
                    concurrent.TryPublish(delegate { entered.Set(); release.WaitOne(1000); return Colors(); });
                });
                first.Start();
                Check(entered.WaitOne(1000), "First callback did not enter the color provider.");
                bool overlappingResult = true;
                var second = new Thread(delegate() { overlappingResult = concurrent.TryPublish(Colors); });
                second.Start();
                try
                {
                    Check(second.Join(250), "Overlapping LED callbacks must never block.");
                    Check(!overlappingResult, "An overlapping callback must drop its frame.");
                }
                finally { release.Set(); first.Join(1000); second.Join(1000); }
            }
            Console.WriteLine("PASS: packet layout, RGB ordering, 90 Hz limit, partial-frame rejection, provider/transport recovery, sequence carry, nonblocking overlap, independent five-model queues/counts/colors.");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("FAIL: " + ex.Message);
            return 1;
        }
    }

    private static void ModelRouting()
    {
        long now=0;
        var queued=new LatestModelFrames();
        int[] counts={88,86,62,76,66};
        for(int model=1;model<=5;++model)
        {
            int id=model,count=counts[model-1];
            var forwarder=new LedForwarder(delegate{return now;},9000,queued.Put,id,count);
            Color[] colors=new Color[count];
            for(int i=0;i<count;++i) colors[i]=Color.FromArgb(id,i,255-i);
            Check(forwarder.TryPublish(delegate{return colors;}),"Each model publishes independently.");
            now+=100;
            colors[0]=Color.FromArgb(id,231,0);
            Check(forwarder.TryPublish(delegate{return colors;}),"Latest same-model snapshot replaces earlier output.");
        }
        for(int model=1;model<=5;++model)
        {
            byte[] packet=queued.Take(model-1);
            Check(packet!=null && packet.Length==20+counts[model-1]*3,"Other model packets must not evict each other.");
            Check(Encoding.ASCII.GetString(packet,0,8)=="OVRLED02" && BitConverter.ToUInt32(packet,16)==model,"Model identity must travel with the physical colors.");
            Check(BitConverter.ToUInt32(packet,8)==2 && BitConverter.ToUInt32(packet,12)==counts[model-1],"Each sequence/count is independent.");
            Check(packet[20]==model && packet[21]==231 && packet[packet.Length-2]==counts[model-1]-1,"Preserve first/last physical RGB.");
            Check(queued.Take(model-1)==null,"A sent model snapshot is not repeated.");
        }
        bool rejected=false;
        try{queued.Put(new byte[1]);}catch(ArgumentException){rejected=true;}
        Check(rejected,"Malformed internal packets cannot select an arbitrary queue.");
    }
}
