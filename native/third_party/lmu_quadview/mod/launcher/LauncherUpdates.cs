using System;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace LmuQuadViewLauncher {
    sealed partial class Launcher {
        readonly Label updateStatus=Theme.Label("Check for new stable releases on GitHub.",10F,Theme.Muted);
        readonly Button updateCheck=Theme.Button("Check for updates",false);
        readonly Button updateAction=Theme.Button("Download update",true);
        readonly CheckBox updateAutomatic=Toggle("Check for updates when the launcher opens");
        UpdateCandidate updateCandidate;
        DownloadedUpdate downloadedUpdate;
        bool updateBusy;
        string UpdatePreferencePath {get{return Path.Combine(root,"update-checks.txt");}}
        Control BuildUpdateCard() {
            var card=new Surface {Name="ApplicationUpdatesCard",Dock=DockStyle.Fill,Margin=new Padding(0,0,0,14)};
            var stack=Stack(5);
            foreach(var height in new[]{32,40,48,55,38})stack.RowStyles.Add(new RowStyle(SizeType.Absolute,height));
            var title=Theme.Label("Application updates",13F,Theme.Text);title.Font=new Font("Segoe UI",13F,FontStyle.Bold);
            stack.Controls.Add(title,0,0);updateAutomatic.Name="UpdateAutomatic";
            try{updateAutomatic.Checked=File.Exists(UpdatePreferencePath)&&File.ReadAllText(UpdatePreferencePath).Trim()=="on";}
            catch(IOException){updateAutomatic.Checked=false;}catch(UnauthorizedAccessException){updateAutomatic.Checked=false;}
            updateAutomatic.CheckedChanged+=delegate {try{File.WriteAllText(UpdatePreferencePath,updateAutomatic.Checked?"on":"off");}catch(Exception error){ShowError(error.Message);}};
            stack.Controls.Add(updateAutomatic,0,1);updateStatus.Name="UpdateStatus";stack.Controls.Add(updateStatus,0,2);
            var actions=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Margin=new Padding(0)};
            actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,50));
            updateCheck.Name="UpdateCheck";updateAction.Name="UpdateAction";
            updateCheck.Dock=updateAction.Dock=DockStyle.Fill;updateAction.Enabled=false;
            updateCheck.Margin=new Padding(0,4,7,4);updateAction.Margin=new Padding(7,4,0,4);
            updateCheck.Click+=delegate {CheckUpdates();};updateAction.Click+=delegate {ApplyUpdate();};
            actions.Controls.Add(updateCheck,0,0);actions.Controls.Add(updateAction,1,0);stack.Controls.Add(actions,0,3);
            stack.Controls.Add(Caption("Downloads and installation need your approval. Close LMU before installing. Your settings and profiles are kept."),0,4);
            card.Controls.Add(stack);return card;
        }
        void InitializeUpdates() {
            if(updateAutomatic.Checked&&Opacity>0&&ShowInTaskbar)CheckUpdates();
        }
        void UpdateWork(Func<object> work,Action<object> done,bool exclusive) {
            if(updateBusy||busy)return;
            updateBusy=true;updateCheck.Enabled=updateAction.Enabled=false;
            if(exclusive){SetBusy(true);launch.Text="Updating...";}
            var worker=new BackgroundWorker();worker.DoWork+=delegate(object sender,DoWorkEventArgs e){e.Result=work();};
            worker.RunWorkerCompleted+=delegate(object sender,RunWorkerCompletedEventArgs e){
                worker.Dispose();if(IsDisposed||Disposing)return;
                updateBusy=false;if(exclusive)SetBusy(false);updateCheck.Enabled=true;
                updateAction.Enabled=downloadedUpdate!=null||updateCandidate!=null;
                if(e.Error!=null){updateStatus.Text="Operation failed: "+e.Error.Message;return;}
                done(e.Result);
            };worker.RunWorkerAsync();
        }
        void CheckUpdates() {
            if(updateBusy||busy)return;
            updateStatus.Text="Checking GitHub Releases...";
            UpdateWork(delegate {return UpdateService.Check(UpdateService.CurrentVersion);},delegate(object result){
                updateCandidate=(UpdateCandidate)result;downloadedUpdate=null;
                updateStatus.Text=updateCandidate==null?"You have the latest stable version ("+UpdateService.CurrentVersion.ToString(3)+").":"Version "+updateCandidate.Version.ToString(3)+" is available.";
                updateAction.Text="Download update";updateAction.Enabled=updateCandidate!=null;
            },false);
        }
        void ApplyUpdate() {
            if(updateBusy||busy)return;
            if(downloadedUpdate==null){
                if(updateCandidate==null)return;
                if(MessageBox.Show(this,"Download the signed installer for version "+updateCandidate.Version.ToString(3)+"? Installation is a separate step.","Download update",MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
                updateStatus.Text="Downloading and verifying the installer...";
                UpdateWork(delegate{return UpdateService.Download(updateCandidate);},delegate(object result){
                    downloadedUpdate=(DownloadedUpdate)result;updateStatus.Text="Verified update ready. Save and close the launcher to install.";
                    updateAction.Text="Install update";updateAction.Enabled=true;
                },true);return;
            }
            if(MessageBox.Show(this,"Save current settings and close this launcher to install the verified update into this package folder? Setup adds Start menu and uninstall entries. LMU must be closed. Reinstall automatic startup from Settings afterwards.","Install update",MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
            try {
                var settings=Values();QvfrSettings.Save(SettingsPath,settings);
                updateStatus.Text="Rechecking the installer before starting setup...";
                UpdateWork(delegate{return UpdateService.StartInstall(downloadedUpdate);},delegate(object result){
                    using(var process=(System.Diagnostics.Process)result){}
                    Close();
                },true);
            }catch(Exception error){updateStatus.Text="Installation stopped: "+error.Message;}
        }
        void CheckStartupIntegrity() {
            if(updateBusy||busy)return;
            UpdateWork(delegate {
                string script=Path.Combine(root,@"tools\check-autostart-integrity.ps1");
                if(!File.Exists(script))throw new FileNotFoundException("Extract the complete package to use this check.",script);
                string directory=Path.Combine(root,"logs");Directory.CreateDirectory(directory);
                string report=Path.Combine(directory,"autostart-integrity-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".json");
                var start=new System.Diagnostics.ProcessStartInfo(Path.Combine(Environment.SystemDirectory,@"WindowsPowerShell\v1.0\powershell.exe"),
                    "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+SteamLaunch.QuoteArgument(script)+" -PackageRoot "+SteamLaunch.QuoteArgument(root)+" -OutputPath "+SteamLaunch.QuoteArgument(report)) {UseShellExecute=false,CreateNoWindow=true};
                using(var process=System.Diagnostics.Process.Start(start)){
                    if(!process.WaitForExit(90000))throw new TimeoutException("Integrity check is still running. Check the logs folder shortly.");
                    if(!File.Exists(report))throw new IOException("Integrity check did not create its report.");
                }
                return report;
            },delegate(object result){status.Text="Integrity report saved. Valid signatures do not guarantee EAC acceptance.";OpenLink((string)result);},true);
        }
    }
}
