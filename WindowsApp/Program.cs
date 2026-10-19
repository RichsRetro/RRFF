using System;

using System.Collections.Generic;

using System.Drawing;

using System.IO;

using System.Linq;

using System.Security.Principal;

using System.Threading;

using System.Threading.Tasks;

using System.Windows.Forms;



namespace RichRetro.Fatx {

static class Program {

    internal static readonly string LogFolder=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RichsRetro","RRXDFormatter");

    internal static readonly string LogPath=Path.Combine(LogFolder,"formatter.log");

    static readonly object LogLock=new object();

    internal static void SaveLog(string text){try{lock(LogLock){Directory.CreateDirectory(LogFolder);File.AppendAllText(LogPath,DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff")+" "+text+Environment.NewLine);}}catch{}}

    internal static bool SaveResult(string status,string text){

        SaveLog(status+": "+text);

        try{

            Directory.CreateDirectory(LogFolder);

            string path=Path.Combine(LogFolder,"last-result.txt");

            File.WriteAllText(path,DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss")+"\r\n"+status+"\r\n"+text);

            return true;

        }catch(Exception e){SaveLog(L.T("Could not save result: ")+e);return false;}

    }

    [STAThread]static void Main(){

        L.Initialize();
        SaveLog("START "+Application.ExecutablePath);

        AppDomain.CurrentDomain.UnhandledException+=delegate(object sender,UnhandledExceptionEventArgs e){SaveLog("UNHANDLED: "+e.ExceptionObject);};

        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);

        try{
            while(true){
                bool explore;
                using(var choice=new StartChoice()){var selection=choice.ShowDialog();if(selection==DialogResult.Retry)continue;if(selection!=DialogResult.OK)break;explore=choice.Explore;}
                if(explore){bool explorerBack;using(var explorer=new ExplorerForm()){Application.Run(explorer);explorerBack=explorer.ReturnToChoices;}if(explorerBack)continue;break;}
                bool returnToChoices;
                using(var formatter=new MainForm()){Application.Run(formatter);returnToChoices=formatter.ReturnToChoices;}
                if(!returnToChoices)break;
            }
            SaveLog("WINDOW CLOSED");
        }

        catch(Exception e){SaveLog("FATAL: "+e);MessageBox.Show(L.T("The formatter stopped unexpectedly. Details were saved to ")+LogPath,L.T("RRFF formatter error"));}

    }

}

sealed class MainForm:LocalizedForm {

    internal bool ReturnToChoices {get;private set;}
    readonly Button back=new Button{Text=L.T("Back"),AutoSize=true};

    readonly ComboBox drives=new ComboBox{DropDownStyle=ComboBoxStyle.DropDownList,Dock=DockStyle.Fill};

    readonly Button refresh=new Button{Text=L.T("Refresh drives"),AutoSize=true};

    readonly Button format=new Button{Text=L.T("Choose C / E / F and format drive"),AutoSize=true};

    readonly Button cancel=new Button{Text=L.T("Cancel"),AutoSize=true,Enabled=false};

    readonly Label details=new Label{Dock=DockStyle.Fill,AutoSize=false};

    readonly Label state=new Label{Dock=DockStyle.Fill,Text="Ready",AutoSize=false};

    readonly ProgressBar bar=new ProgressBar{Dock=DockStyle.Fill};

    readonly TextBox log=new TextBox{Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical,Dock=DockStyle.Fill};

    CancellationTokenSource cancellation;bool busy;DateTime lastUpdate=DateTime.MinValue;

    public MainForm() {

        Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);

        Text="Rich's Retro FATX Formatter (RRFF)";Size=new Size(940,680);MinimumSize=new Size(790,600);StartPosition=FormStartPosition.CenterScreen;Font=new Font("Segoe UI",11);

        BackColor=Color.FromArgb(12,20,30);ForeColor=Color.FromArgb(220,230,235);

        var root=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(20),ColumnCount=1,RowCount=9};Controls.Add(root);

        root.RowStyles.Add(new RowStyle(SizeType.Absolute,58));root.RowStyles.Add(new RowStyle(SizeType.Absolute,65));root.RowStyles.Add(new RowStyle(SizeType.Absolute,42));root.RowStyles.Add(new RowStyle(SizeType.Absolute,82));root.RowStyles.Add(new RowStyle(SizeType.Absolute,55));root.RowStyles.Add(new RowStyle(SizeType.Absolute,52));root.RowStyles.Add(new RowStyle(SizeType.Absolute,26));root.RowStyles.Add(new RowStyle(SizeType.Percent,100));root.RowStyles.Add(new RowStyle(SizeType.Absolute,42));

        root.Controls.Add(new Label{Text="RRFF   |   Rich's Retro FATX Formatter",Font=new Font("Segoe UI",21,FontStyle.Bold),ForeColor=Color.Cyan,Dock=DockStyle.Fill},0,0);

        root.Controls.Add(new Label{Text=L.T("Prepare an original Xbox HDD with C, E, cache partitions and F (plus G on large drives) using the remaining supported space. Includes Rich's Retro XDASH for a hardmodded Xbox."),Dock=DockStyle.Fill},0,1);

        root.Controls.Add(drives,0,2);root.Controls.Add(details,0,3);

        var buttons=new FlowLayoutPanel{Dock=DockStyle.Fill};buttons.Controls.AddRange(new Control[]{refresh,format,cancel,back});root.Controls.Add(buttons,0,4);

        root.Controls.Add(state,0,5);root.Controls.Add(bar,0,6);root.Controls.Add(log,0,7);

        var coffee=new LinkLabel{Text=L.T("Buy Rich a coffee"),AutoSize=true,LinkColor=Color.Cyan,Dock=DockStyle.Fill};coffee.LinkClicked+=delegate{System.Diagnostics.Process.Start("https://buymeacoffee.com/richsretro");};root.Controls.Add(coffee,0,8);

        foreach(var b in new[]{refresh,format,cancel,back}){b.BackColor=Color.FromArgb(22,55,70);b.ForeColor=ForeColor;b.FlatStyle=FlatStyle.Flat;}

        log.BackColor=Color.FromArgb(8,14,22);log.ForeColor=ForeColor;

        refresh.Click+=async delegate{await Scan();};format.Click+=async delegate{await StartFormat();};cancel.Click+=delegate{if(MessageBox.Show(this,L.T("Cancel drive setup? The drive may be incomplete and need formatting again."),L.T("Cancel"),MessageBoxButtons.YesNo,MessageBoxIcon.Warning,MessageBoxDefaultButton.Button2)==DialogResult.Yes)cancellation.Cancel();};

        back.Click+=delegate{if(busy)return;ReturnToChoices=true;Close();};

        drives.SelectedIndexChanged+=delegate{ShowTarget();};Shown+=async delegate{await Scan();};FormClosing+=delegate(object sender,FormClosingEventArgs e){if(busy){e.Cancel=true;MessageBox.Show(this,L.T("Wait for setup to finish, or cancel it first."));}};

    }

    void Busy(bool value){busy=value;back.Enabled=!value;refresh.Enabled=!value;drives.Enabled=!value;format.Enabled=!value && drives.SelectedItem!=null;cancel.Enabled=value;}

    void Note(string text){text=(text??String.Empty).Replace("\r\n","\n").Replace("\r","\n").Replace("\n",Environment.NewLine);log.AppendText(text+Environment.NewLine);Program.SaveLog(text);}

    async Task Scan(){Busy(true);cancel.Enabled=false;state.Text=L.T("Finding non-system drives...");try{var all=await Task.Run(()=>Storage.Scan());drives.Items.Clear();foreach(var d in all.Where(x=>!x.Protected))drives.Items.Add(d);drives.SelectedIndex=-1;details.Text=L.T("Choose a drive. Windows, boot and page-file disks are excluded.");state.Text=drives.Items.Count+L.T(" non-system drive(s) available.");Note(L.T("Drive list refreshed. No disk contents were changed."));}catch(Exception e){drives.Items.Clear();state.Text=L.T("Drive discovery blocked: ")+e.Message;}finally{Busy(false);}}

    void ShowTarget(){var d=drives.SelectedItem as Target;details.Text=d==null?L.T("Choose a drive."):d+Environment.NewLine+L.T("All existing partitions and files on this drive will be removed. F uses the remaining space; large drives offer F/G allocation.");format.Enabled=!busy && d!=null;}

    void UpdateProgress(string message,long done,long total){if(IsDisposed)return;if((DateTime.UtcNow-lastUpdate).TotalMilliseconds<150)return;lastUpdate=DateTime.UtcNow;BeginInvoke((Action)delegate{if(!busy)return;int percent=total<=0?0:(int)Math.Min(99,done*100/total);bar.Style=total<=0?ProgressBarStyle.Marquee:ProgressBarStyle.Continuous;state.Text=message+(total<=0?"":"  -  "+percent+"%");bar.Value=percent;});}

    async Task<bool> ChooseGameLayout(InstallPlan plan){
        foreach(string partition in new[]{"F","G"}){
            string folder=partition=="F"?plan.FFolder:plan.GFolder;if(folder==null)continue;
            int kind=await Task.Run(()=>{if(Directory.Exists(Path.Combine(folder,"Games")))return 0;if(new DirectoryInfo(folder).Name.Equals("Games",StringComparison.OrdinalIgnoreCase))return 1;if(File.Exists(Path.Combine(folder,"default.xbe")))return 2;return Directory.EnumerateDirectories(folder).Any(d=>File.Exists(Path.Combine(d,"default.xbe")))?3:0;});
            string prefix="";bool loose=false;
            if(kind==1)prefix="Games";
            else if(kind>=2){var choice=MessageBox.Show(this,L.T("The selected ")+partition+L.T(" source contains ")+(kind==2?L.T("an executable"):L.T("loose executable folders"))+L.T(" but no Games folder.\n\nTreat ")+(kind==2?L.T("this folder"):L.T("these loose executable folders"))+L.T(" as games and put ")+(kind==2?"it":"them")+L.T(" under ")+partition+L.T(":\\Games?\n\nYes: place games under Games. No: keep your selected structure (use this for app folders). Cancel: return without formatting. XBE files can be games or apps, so this choice is yours."),L.T("Choose game-folder layout"),MessageBoxButtons.YesNoCancel,MessageBoxIcon.Question,MessageBoxDefaultButton.Button3);
                if(choice==DialogResult.Cancel)return false;if(choice==DialogResult.Yes){if(kind==3)loose=true;else{string name=new DirectoryInfo(folder).Name;name=System.Text.RegularExpressions.Regex.Replace(name,@"\s*[\(\[].*$","").Trim();if(name.Length>42)name=name.Substring(0,42).TrimEnd();FatxNames.Validate(name);prefix="Games/"+name;}}
            }
            if(partition=="F"){plan.FPrefix=prefix;plan.FLooseGames=loose;}else{plan.GPrefix=prefix;plan.GLooseGames=loose;}
        }return true;
    }
    async Task StartFormat(){
        if(busy)return;

        var d=drives.SelectedItem as Target;if(d==null)return;

        if(!new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator)) {

            if(MessageBox.Show(this,L.T("Administrator access is needed. Reopen the formatter as administrator? You will choose and confirm the drive again."),L.T("Administrator access"),MessageBoxButtons.YesNo,MessageBoxIcon.Information)==DialogResult.Yes)try{System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(Application.ExecutablePath){UseShellExecute=true,Verb="runas"});Close();}catch(Exception e){MessageBox.Show(e.Message);}return;

        }

        bool split=Fatx.NeedsG(d.Size),automatic=false;

        if(split)using(var layout=new SplitLayoutDialog(d.Size)){var choice=layout.ShowDialog(this);if(choice!=DialogResult.Yes&&choice!=DialogResult.No)return;automatic=choice==DialogResult.Yes;}

        InstallPlan plan;using(var wizard=new SetupWizard(split,automatic)){if(wizard.ShowDialog(this)!=DialogResult.OK)return;plan=wizard.Plan;}

        try{Busy(true);cancel.Enabled=false;if(!await ChooseGameLayout(plan))return;}catch(Exception e){MessageBox.Show(this,e.Message,L.T("Source layout check"));return;}finally{Busy(false);}
        cancellation=new CancellationTokenSource();Busy(true);bar.Value=0;state.Text=L.T("Checking selected drive and bundled files...");

        try {

            Fatx writer=null;var patchConsent=new GamePatchConsent(this);

            while(writer==null){

                try{writer=await Task.Run(()=>{Storage.ProtectSources(d,plan);if(d.Sector!=512)throw new IOException(L.T("This drive exposes ")+d.Sector+L.T("-byte logical sectors. Original Xbox formatting requires 512-byte logical sectors."));return new Fatx(d.Size,plan,UpdateProgress,cancellation.Token,patchConsent.Ask);});}

                catch(CConflictException conflict){

                    cancellation.Token.ThrowIfCancellationRequested();

                    using(var choice=new CConflictDialog(conflict.RelativePath)){

                        var decision=choice.ShowDialog(this);

                        if(decision==DialogResult.Yes){plan.SkipCConflicts=true;Note(L.T("Keeping RRXD and skipping conflicting custom C items. Folder choices retained."));}

                        else if(decision==DialogResult.No){plan.Rrxd=false;plan.SkipCConflicts=false;Note(L.T("Using your own C folder without RRXD. E and F choices retained."));}

                        else{state.Text=L.T("Cancelled  -  no disk contents changed.");return;}

                    }

                }

            }

            cancellation.Token.ThrowIfCancellationRequested();patchConsent.Confirm();bar.Style=ProgressBarStyle.Continuous;state.Text=L.T("Checks passed  -  no disk contents changed yet.");Note(writer.Summary);

            if(MessageBox.Show(this,L.T("Erase and set up this drive?\n\n")+d+"\n\n"+writer.Summary+L.T("\n\nEvery existing file and partition on this selected drive will be lost."),L.T("Confirm selected drive"),MessageBoxButtons.YesNo,MessageBoxIcon.Warning,MessageBoxDefaultButton.Button2)!=DialogResult.Yes)return;

            if(MessageBox.Show(this,L.T("Are you really sure?\n\n")+d+L.T("\n\nThis cannot be undone. Only continue if this is the Xbox target drive."),L.T("Final erase confirmation"),MessageBoxButtons.YesNo,MessageBoxIcon.Warning,MessageBoxDefaultButton.Button2)!=DialogResult.Yes)return;

            if(!Program.SaveResult("IN PROGRESS","Setup started for "+d+L.T(". Completion is not confirmed yet.")))throw new IOException(L.T("Cannot save the setup result. No formatting started."));

            await Task.Run(()=>writer.Run(d));

            bool resultSaved=Program.SaveResult("SUCCESS",writer.CompletionSummary+"\r\n"+(plan.FullVerify?L.T("Full read-back completed."):L.T("Partition structure and C/E verified; F/G content read-back not selected.")));

            bar.Style=ProgressBarStyle.Continuous;bar.Value=100;state.Text=plan.FullVerify?L.T("Complete  -  partitions and all copied files verified."):L.T("Complete  -  files copied; partition structure and C/E verified.");state.Text+="\n"+writer.CompletionSummary;if(!resultSaved)Note(L.T("Drive setup completed, but the saved result could not be written."));Note(L.T("Completed at ")+DateTime.Now.ToString("HH:mm:ss")+"  -  "+writer.CompletionSummary);Note(plan.FullVerify?L.T("Full read-back verification completed."):L.T("Quick verification completed. F/G file contents were copied without a full read-back pass."));Note(L.T("Drive setup completed successfully. The selected C, E, cache and F/G layout is installed. Windows cannot browse FATX; cancel any Windows initialise/format prompt. Custom dashboards still need a compatible BIOS boot path and any required HDD locking."));

        }catch(OperationCanceledException){Program.SaveResult("CANCELLED",L.T("Drive setup is incomplete."));state.Text=L.T("Cancelled  -  drive setup is incomplete.");Note(state.Text);}

        catch(Exception e){Program.SaveResult("FAILED",e.ToString());state.Text=L.T("Failed  -  drive setup did not complete.");Note(e.Message+Environment.NewLine+L.T("Do not treat this drive as ready. Fix the reported issue before retrying."));}

        finally{Busy(false);bar.Style=ProgressBarStyle.Continuous;cancellation.Dispose();cancellation=null;}

    }

}

}

