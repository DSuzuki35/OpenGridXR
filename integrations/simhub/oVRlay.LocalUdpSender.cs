using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;

namespace oVRlay.SimHubLeds
{
    internal sealed class LocalUdpSender : IDisposable
    {
        private readonly AutoResetEvent ready = new AutoResetEvent(false);
        private readonly Thread worker;
        private readonly LatestModelFrames pending=new LatestModelFrames();
        private volatile bool stopping;
        private volatile string lastError;

        public LocalUdpSender()
        {
            worker = new Thread(Run);
            worker.IsBackground = true;
            worker.Name = "oVRlay local LED sender";
            worker.Start();
        }

        public string LastError { get { return lastError; } }

        public void Queue(byte[] packet)
        {
            if (stopping) throw new ObjectDisposedException("Local LED sender");
            // One latest snapshot per model prevents another wheel from evicting its colors.
            pending.Put(packet);
            ready.Set();
        }

        private void Run()
        {
            Socket socket = null;
            try
            {
                socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                socket.Bind(new IPEndPoint(IPAddress.Loopback, 0));
                socket.Blocking = false;
                EndPoint destination = new IPEndPoint(IPAddress.Loopback, 29543);
                while (!stopping)
                {
                    ready.WaitOne(250);
                    if (stopping) break;
                    for(int model=0;model<5 && !stopping;++model)
                    {
                        byte[] packet = pending.Take(model);
                        if (packet == null) continue;
                        try
                        {
                            socket.SendTo(packet, destination);
                            lastError = null;
                        }
                        catch (SocketException ex) { lastError = "Local connection: " + ex.SocketErrorCode; }
                    }
                }
            }
            catch (Exception ex) { lastError = "Local sender unavailable: " + ex.Message; }
            finally
            {
                if (socket != null) socket.Dispose();
                ready.Dispose();
            }
        }

        public void Dispose()
        {
            stopping = true;
            try { ready.Set(); } catch (ObjectDisposedException) { }
            worker.Join(500);
            pending.Clear();
        }
    }
}
