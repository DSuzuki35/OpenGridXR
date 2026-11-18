using System;
using System.Drawing;
using System.Threading;

namespace oVRlay.SimHubLeds
{
    internal sealed class LedForwarder
    {
        private readonly Func<long> clock;
        private readonly Action<byte[]> sink;
        private readonly long interval;
        private readonly object gate = new object();
        private long previous;
        private bool hasPrevious;
        private uint sequence;
        private readonly int modelId;
        private readonly int ledCount;

        public LedForwarder(Func<long> clock, long frequency, Action<byte[]> sink)
            : this(clock,frequency,sink,0,88) { }

        public LedForwarder(Func<long> clock, long frequency, Action<byte[]> sink, int modelId, int ledCount)
        {
            if(modelId<0 || modelId>5 || ledCount<1 || ledCount>96 || (modelId==0 && ledCount!=88))
                throw new ArgumentException("Unsupported LED model/count.");
            this.clock = clock;
            this.sink = sink;
            this.modelId=modelId;
            this.ledCount=ledCount;
            interval = Math.Max(1, (frequency + 89) / 90);
        }

        public string LastError { get; private set; }

        public bool TryPublish(Func<Color[]> readColors)
        {
            // An overlapping callback is dropped instead of waiting on SimHub's LED thread.
            if (!Monitor.TryEnter(gate)) return false;
            try
            {
                long now = clock();
                if (hasPrevious && now - previous < interval) return false;
                previous = now;
                hasPrevious = true;
                Color[] colors = readColors();
                if (colors == null || colors.Length != ledCount)
                    throw new InvalidOperationException("Waiting for a complete "+ledCount+"-light frame.");

                int header=modelId==0?16:20;
                byte[] packet = new byte[header+ledCount*3];
                byte[] magic = modelId==0 ? new byte[]{65,71,76,69,68,48,48,49} : new byte[]{79,86,82,76,69,68,48,50};
                Buffer.BlockCopy(magic, 0, packet, 0, 8);
                uint next = unchecked(sequence + 1);
                WriteUInt32(packet, 8, next);
                WriteUInt32(packet, 12, (uint)ledCount);
                if(modelId!=0) WriteUInt32(packet,16,(uint)modelId);
                for (int i = 0; i < colors.Length; ++i)
                {
                    packet[header + i * 3] = colors[i].R;
                    packet[header + 1 + i * 3] = colors[i].G;
                    packet[header + 2 + i * 3] = colors[i].B;
                }
                sink(packet);
                sequence = next;
                LastError = null;
                return true;
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                return false;
            }
            finally { Monitor.Exit(gate); }
        }

        private static void WriteUInt32(byte[] packet, int offset, uint value)
        {
            packet[offset] = (byte)value;
            packet[offset + 1] = (byte)(value >> 8);
            packet[offset + 2] = (byte)(value >> 16);
            packet[offset + 3] = (byte)(value >> 24);
        }
    }

    internal sealed class LatestModelFrames
    {
        private readonly byte[][] pending=new byte[5][];
        public void Put(byte[] packet)
        {
            if(packet==null || packet.Length<20) throw new ArgumentException("Invalid LED packet.");
            bool legacy=packet[0]==65 && packet[1]==71 && packet[2]==76 && packet[3]==69 && packet[4]==68 && packet[5]==48 && packet[6]==48 && packet[7]==49;
            bool routed=packet[0]==79 && packet[1]==86 && packet[2]==82 && packet[3]==76 && packet[4]==69 && packet[5]==68 && packet[6]==48 && packet[7]==50;
            uint id=legacy?1:BitConverter.ToUInt32(packet,16);
            uint count=BitConverter.ToUInt32(packet,12);
            if((!legacy && !routed) || id<1 || id>5 || count<1 || count>96 || packet.Length!=(legacy?16:20)+count*3)
                throw new ArgumentException("Invalid model LED packet.");
            Interlocked.Exchange(ref pending[id-1],packet);
        }
        public byte[] Take(int index) { return Interlocked.Exchange(ref pending[index],null); }
        public void Clear() { for(int i=0;i<5;++i) Interlocked.Exchange(ref pending[i],null); }
    }
}
