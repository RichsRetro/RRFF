using System;

using System.Collections.Generic;

using System.Diagnostics;

using System.Drawing;

using System.IO;

using System.Linq;

using System.Threading;

using System.Threading.Tasks;

using System.Windows.Forms;

namespace RichRetro.Fatx {

internal sealed class StartChoice:LocalizedForm {

    public bool Explore;

    public StartChoice(){Text="Rich's Retro FATX Formatter (RRFF)";ClientSize=new Size(680,390);StartPosition=FormStartPosition.CenterScreen;Font=new Font("Segoe UI",12);BackColor=Color.FromArgb(12,20,30);ForeColor=Color.FromArgb(220,230,235);Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);

        var heading=new Label{Text=L.T("What would you like to do?"),Font=new Font("Segoe UI",21,FontStyle.Bold),ForeColor=Color.Cyan,Location=new Point(25,25),Size=new Size(630,55)};

        var format=new Button{Text=L.T("Format a new HDD"),Location=new Point(25,95),Size=new Size(630,55)};

        var explore=new Button{Text=L.T("Explore an existing FATX HDD"),Location=new Point(25,165),Size=new Size(630,55)};

        var note=new Label{Text=L.T("Windows and system drives are excluded."),Location=new Point(25,240),Size=new Size(630,45)};

        foreach(var button in new[]{format,explore}){button.FlatStyle=FlatStyle.Flat;button.BackColor=Color.FromArgb(22,55,70);button.ForeColor=ForeColor;}

        format.Click+=delegate{Explore=false;DialogResult=DialogResult.OK;Close();};explore.Click+=delegate{Explore=true;DialogResult=DialogResult.OK;Close();};Controls.AddRange(new Control[]{heading,format,explore,note});
        var languageLabel=new Label{Text=L.T("Language"),Location=new Point(25,305),AutoSize=true};
        var language=new ComboBox{DropDownStyle=ComboBoxStyle.DropDownList,Location=new Point(220,300),Width=430};
        foreach(string name in L.Names)language.Items.Add(L.T(name));
        language.SelectedIndex=Array.IndexOf(L.Codes,L.Preference);
        language.SelectedIndexChanged+=delegate{if(!L.Set(L.Codes[language.SelectedIndex],true)){MessageBox.Show(this,L.T("The language changed, but the preference could not be saved."));}DialogResult=DialogResult.Retry;Close();};
        Controls.Add(languageLabel);Controls.Add(language);


    }

}

internal sealed class DragBadge:LocalizedForm {
    public readonly Label Message=new Label{Dock=DockStyle.Fill,TextAlign=ContentAlignment.MiddleLeft,Padding=new Padding(10)};
    public DragBadge(){FormBorderStyle=FormBorderStyle.None;ShowInTaskbar=false;TopMost=true;StartPosition=FormStartPosition.Manual;Size=new Size(270,44);BackColor=Color.FromArgb(22,55,70);ForeColor=Color.Cyan;Font=new Font("Segoe UI",10,FontStyle.Bold);Controls.Add(Message);}
    protected override bool ShowWithoutActivation{get{return true;}}
    protected override CreateParams CreateParams{get{var p=base.CreateParams;p.ExStyle|=0x08000000|0x80|0x20;return p;}}
}

internal sealed class ExplorerForm:LocalizedForm {
    internal bool ReturnToChoices {get;private set;}
    readonly Button backToChoices=new Button{Text=L.T("Back"),AutoSize=true};

    readonly ComboBox drives=new ComboBox{Width=640,DropDownStyle=ComboBoxStyle.DropDownList};

    readonly ComboBox partitions=new ComboBox{Width=130,DropDownStyle=ComboBoxStyle.DropDownList};

    readonly Button compatibility=new Button{Text=L.T("Game compatibility"),AutoSize=true};

    readonly Button check=new Button{Text=L.T("Check FATX drive"),AutoSize=true};

    readonly Button connect=new Button{Text=L.T("Open HDD"),AutoSize=true},refresh=new Button{Text=L.T("Refresh drives"),AutoSize=true};

    readonly Button up=new Button{Text=L.T("Up"),AutoSize=true},addFiles=new Button{Text=L.T("Add files"),AutoSize=true},addFolder=new Button{Text=L.T("Add folder"),AutoSize=true},export=new Button{Text=L.T("Copy to PC"),AutoSize=true},newFolder=new Button{Text=L.T("New folder"),AutoSize=true},delete=new Button{Text=L.T("Delete"),AutoSize=true},reload=new Button{Text=L.T("Refresh folder"),AutoSize=true},cancel=new Button{Text=L.T("Cancel operation"),AutoSize=true};

    readonly ListView files=new ListView{Dock=DockStyle.Fill,View=View.Details,FullRowSelect=true,HideSelection=false,MultiSelect=true};

    readonly Label path=new Label{Dock=DockStyle.Fill,AutoEllipsis=true},status=new Label{Dock=DockStyle.Fill};

    readonly TextBox reportBox=new TextBox{Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Both,Dock=DockStyle.Fill,WordWrap=false,Visible=false};

    readonly SplitContainer contentSplit=new SplitContainer{Dock=DockStyle.Fill,Orientation=Orientation.Horizontal,Size=new Size(1000,400),SplitterDistance=300,Panel1MinSize=120,Panel2MinSize=68};

    readonly ProgressBar bar=new ProgressBar{Dock=DockStyle.Fill};readonly Stack<XboxEntry> folders=new Stack<XboxEntry>();

    ExplorerDisk session;Target target;CancellationTokenSource cancellation;bool busy,changing;DateTime lastProgress=DateTime.MinValue;Stopwatch watch;

    public ExplorerForm(){Text=L.T("RRFF  -  FATX File Manager");Size=new Size(1080,720);MinimumSize=new Size(850,600);StartPosition=FormStartPosition.CenterScreen;Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);Font=new Font("Segoe UI",11);BackColor=Color.FromArgb(12,20,30);ForeColor=Color.FromArgb(220,230,235);

        var root=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(18),RowCount=7,ColumnCount=1};Controls.Add(root);foreach(int height in new[]{45,45,38,50})root.RowStyles.Add(new RowStyle(SizeType.Absolute,height));root.RowStyles.Add(new RowStyle(SizeType.Percent,100));root.RowStyles.Add(new RowStyle(SizeType.Absolute,25));root.RowStyles.Add(new RowStyle(SizeType.Absolute,44));

        var driveRow=new FlowLayoutPanel{Dock=DockStyle.Fill};driveRow.Controls.AddRange(new Control[]{drives,refresh,connect});root.Controls.Add(driveRow,0,0);

        var partitionRow=new FlowLayoutPanel{Dock=DockStyle.Fill};partitionRow.Controls.AddRange(new Control[]{new Label{Text=L.T("Partition"),AutoSize=true,Padding=new Padding(0,7,0,0)},partitions,up,reload,check,compatibility});root.Controls.Add(partitionRow,0,1);root.Controls.Add(path,0,2);

        var operations=new FlowLayoutPanel{Dock=DockStyle.Fill};operations.Controls.AddRange(new Control[]{addFiles,addFolder,export,newFolder,delete,cancel});root.Controls.Add(operations,0,3);

        files.Columns.Add("Name",590);files.Columns.Add("Type",130);files.Columns.Add("Size",160);files.BackColor=Color.FromArgb(8,14,22);files.ForeColor=ForeColor;contentSplit.Panel1.Controls.Add(files);status.Dock=DockStyle.Top;status.Height=60;

        reportBox.BackColor=files.BackColor;reportBox.ForeColor=ForeColor;reportBox.Font=new Font("Consolas",10);contentSplit.Panel2.Controls.Add(reportBox);contentSplit.Panel2.Controls.Add(status);

        root.Controls.Add(contentSplit,0,4);root.Controls.Add(bar,0,5);

        var coffee=new LinkLabel{Text=L.T("Buy Rich a coffee"),AutoSize=true,LinkColor=Color.Cyan,Dock=DockStyle.Fill};coffee.LinkClicked+=delegate{try{Process.Start("https://buymeacoffee.com/richsretro");}catch(Exception e){Error(e);}};var footer=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=1};footer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));footer.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));footer.Controls.Add(coffee,0,0);footer.Controls.Add(backToChoices,1,0);root.Controls.Add(footer,0,6);

        foreach(var button in new[]{backToChoices,compatibility,check,connect,refresh,up,addFiles,addFolder,export,newFolder,delete,reload,cancel}){button.FlatStyle=FlatStyle.Flat;button.BackColor=Color.FromArgb(22,55,70);button.ForeColor=ForeColor;}

        backToChoices.Click+=delegate{if(busy)return;ReturnToChoices=true;Close();};
        compatibility.Click+=delegate{ShowInlineReport(GameCompatibility.KnownCases());};check.Click+=async delegate{await CheckDrive();};drives.SelectedIndexChanged+=delegate{check.Enabled=!busy&&drives.SelectedItem!=null;};refresh.Click+=async delegate{await Scan();};connect.Click+=async delegate{await Open();};partitions.SelectedIndexChanged+=async delegate{if(!changing)await ChangePartition();};up.Click+=async delegate{if(folders.Count>1){folders.Pop();await ListFolder();}};reload.Click+=async delegate{await ListFolder();};

        files.DoubleClick+=async delegate{if(!busy&&files.SelectedItems.Count==1){var entry=files.SelectedItems[0].Tag as XboxEntry;if(entry.Directory){folders.Push(entry);await ListFolder();}}};

        addFiles.Click+=async delegate{using(var pick=new OpenFileDialog{Title=L.T("Add files to ")+path.Text,Multiselect=true}){if(pick.ShowDialog(this)==DialogResult.OK){var paths=pick.FileNames;await Operation("Adding files",(p)=>ImportWithPatchList(folders.Peek(),paths,p));}}};

        addFolder.Click+=async delegate{try{string source=FolderPicker.Choose(this,'F',"");if(source!=null)await Operation("Adding folder",p=>ImportWithPatchList(folders.Peek(),new[]{source},p));}catch(Exception e){Error(e);}};

        export.Click+=async delegate{
            if(busy||session==null)return;var entries=Selected();if(entries.Length==0){status.Text=L.T("Select the files or folders you want to copy first. Ctrl+A selects all.");return;}
            string destination=null;bool pickerFailed=false;Busy(true);cancel.Enabled=false;status.Text=L.T("Opening PC destination picker...");bar.Style=ProgressBarStyle.Marquee;Program.SaveLog("PC destination picker opening");
            try{destination=await FolderPicker.ChooseAsync(this,'E',"",Program.SaveLog);}catch(Exception e){pickerFailed=true;Error(e);}finally{Busy(false);bar.Style=ProgressBarStyle.Continuous;bar.Value=0;}
            if(destination!=null)await Operation("Copying to PC",p=>session.Export(entries,destination,p));else if(!pickerFailed)status.Text=L.T("Copy to PC cancelled before copying started.");
        };

        newFolder.Click+=async delegate{string name=AskName();if(name!=null)await Operation("Creating folder",p=>{session.GuardWrites(p);session.NewFolder(folders.Peek(),name);});};

        delete.Click+=async delegate{var entries=Selected();if(entries.Length==0)return;if(MessageBox.Show(this,L.T("Permanently delete ")+entries.Length+L.T(" selected item(s) and any contents from ")+path.Text+"?\n\n"+String.Join("\n",entries.Take(8).Select(x=>x.Name))+L.T("\n\nThis cannot be undone."),L.T("Confirm FATX deletion"),MessageBoxButtons.YesNo,MessageBoxIcon.Warning,MessageBoxDefaultButton.Button2)==DialogResult.Yes)await Operation("Deleting",p=>session.Delete(entries,p),false);};

        cancel.Click+=delegate{if(cancellation!=null&&MessageBox.Show(this,L.T("Cancel this operation? Completed files stay in place; unfinished transfers may need to be retried."),L.T("Cancel operation"),MessageBoxButtons.YesNo,MessageBoxIcon.Question,MessageBoxDefaultButton.Button2)==DialogResult.Yes)cancellation.Cancel();};

        FormClosing+=delegate(object sender,FormClosingEventArgs e){if(busy){e.Cancel=true;MessageBox.Show(this,L.T("Wait for the operation to finish, or cancel it first."));}else{if(session!=null){session.Dispose();session=null;}if(cancellation!=null){cancellation.Dispose();cancellation=null;}}};

        InstallDragAndClipboard();Shown+=async delegate{await Scan();};Busy(false);

    }

    sealed class DiskClipboard {public ExplorerDisk Session;public string Partition;public XboxEntry Parent;public XboxEntry[] Entries;public bool Cut;}

    DragBadge dragBadge;
    DiskClipboard clipboard,mouseDrag;readonly System.Windows.Forms.Timer dragTimer=new System.Windows.Forms.Timer{Interval=40};

    ShellFileDrop shellFormDrop,shellListDrop;

    void CaptureSelection(bool cut){if(busy||session==null||folders.Count==0)return;var selected=Selected();if(selected.Length==0)return;clipboard=new DiskClipboard{Session=session,Partition=session.PartitionKey,Parent=folders.Peek(),Entries=selected,Cut=cut};status.Text=selected.Length+L.T(" item(s) ready to ")+(cut?"move":"copy")+L.T(". Open the destination and Paste.");}

    async Task PasteInto(XboxEntry destination,DiskClipboard data,bool move){

        if(data==null||busy||session==null)return;if(data.Session!=session||data.Partition!=session.PartitionKey){MessageBox.Show(this,L.T("This clipboard belongs to another drive or partition. Select the items again in the current partition."));return;}

        if(move&&data.Parent.First==destination.First)return;

        await Operation(move?"Moving":"Copying",p=>{if(move)session.MoveWithin(data.Parent,data.Entries,destination,p);else session.CopyWithin(data.Parent,data.Entries,destination,p);},!move);

        if(move)clipboard=null;

    }

    XboxEntry DropFolder(DragEventArgs e){var point=files.PointToClient(new Point(e.X,e.Y));var row=files.GetItemAt(point.X,point.Y);if(row==null)return folders.Count==0?null:folders.Peek();var entry=row.Tag as XboxEntry;return entry!=null&&entry.Directory?entry:null;}

    XboxEntry MouseDropFolder(Point screen){

        if(folders.Count==0)return null;

        if(up.RectangleToScreen(up.ClientRectangle).Contains(screen)&&folders.Count>1)return folders.ToArray()[1];

        if(!files.RectangleToScreen(files.ClientRectangle).Contains(screen))return null;

        var point=files.PointToClient(screen);var row=files.GetItemAt(point.X,point.Y);

        if(row==null)return folders.Peek();var entry=row.Tag as XboxEntry;if(mouseDrag!=null&&entry!=null&&mouseDrag.Entries.Any(x=>x.Slot==entry.Slot))return null;return entry!=null&&entry.Directory?entry:null;

    }

    void InstallDragAndClipboard(){

        Action<string[],Point> shellDrop=async (sources,point)=>{if(busy||session==null||folders.Count==0)return;var row=files.GetItemAt(files.PointToClient(point).X,files.PointToClient(point).Y);var destination=row==null?folders.Peek():row.Tag as XboxEntry;if(destination==null||!destination.Directory)return;await Operation("Adding dropped files",p=>ImportWithPatchList(destination,sources,p));};

        shellFormDrop=new ShellFileDrop(this,shellDrop);shellListDrop=new ShellFileDrop(files,shellDrop);

        var context=new ContextMenuStrip();context.Items.Add(L.T("Cut"),null,delegate{CaptureSelection(true);});context.Items.Add(L.T("Copy"),null,delegate{CaptureSelection(false);});context.Items.Add(L.T("Paste"),null,async delegate{if(folders.Count>0)await PasteInto(folders.Peek(),clipboard,clipboard!=null&&clipboard.Cut);});context.Items.Add(new ToolStripSeparator());context.Items.Add(L.T("Delete..."),null,delegate{if(!busy)delete.PerformClick();});files.ContextMenuStrip=context;

        KeyPreview=true;KeyDown+=async delegate(object sender,KeyEventArgs e){if(e.KeyCode==Keys.Escape&&mouseDrag!=null){mouseDrag=null;dragTimer.Stop();files.Cursor=Cursors.Default;HideDragBadge();e.SuppressKeyPress=true;return;}if(busy||session==null)return;if(e.Control&&e.KeyCode==Keys.A&&files.ContainsFocus){files.BeginUpdate();foreach(ListViewItem item in files.Items)item.Selected=true;files.EndUpdate();e.SuppressKeyPress=true;}else if(e.Control&&e.KeyCode==Keys.X){CaptureSelection(true);e.SuppressKeyPress=true;}else if(e.Control&&e.KeyCode==Keys.C){CaptureSelection(false);e.SuppressKeyPress=true;}else if(e.Control&&e.KeyCode==Keys.V&&folders.Count>0){e.SuppressKeyPress=true;await PasteInto(folders.Peek(),clipboard,clipboard!=null&&clipboard.Cut);}else if(e.KeyCode==Keys.Delete){e.SuppressKeyPress=true;delete.PerformClick();}};

        // OLE registration blocks lower-integrity Explorer sources before

        // WM_DROPFILES can reach the native handler. Internal drags stay local.

        files.AllowDrop=false;up.AllowDrop=false;

        files.ItemDrag+=delegate{if(busy||session==null||folders.Count==0)return;var entries=Selected();if(entries.Length==0)return;mouseDrag=new DiskClipboard{Session=session,Partition=session.PartitionKey,Parent=folders.Peek(),Entries=entries};ShowDragBadge(entries.Length+L.T(" selected item(s)"));status.Text=L.T("Drag onto a folder to move; hold Ctrl to copy. Use Copy to PC for Windows exports.");dragTimer.Start();};

        // The native list releases capture after its begin-drag notification.
        // Poll the real button state so that synthetic MouseUp cannot move items.
        dragTimer.Tick+=async delegate{
            if(mouseDrag==null){dragTimer.Stop();return;}
            var data=mouseDrag;var screen=Cursor.Position;MoveDragBadge();
            if((Control.MouseButtons&MouseButtons.Left)==0){
                var destination=MouseDropFolder(screen);mouseDrag=null;dragTimer.Stop();files.Cursor=Cursors.Default;HideDragBadge();
                if(destination!=null&&!data.Entries.Any(x=>x.Slot==destination.Slot))await PasteInto(destination,data,(ModifierKeys&Keys.Control)==0);
                return;
            }
            if(!RectangleToScreen(ClientRectangle).Contains(screen)){
                mouseDrag=null;dragTimer.Stop();files.Cursor=Cursors.Default;HideDragBadge();status.Text=L.T("Drag to Windows is temporarily disabled. Select Copy to PC to export these items.");return;
            }
            ShowDragBadge(((ModifierKeys&Keys.Control)!=0?L.T("Copy "):L.T("Move "))+data.Entries.Length+L.T(" selected item(s)"));files.Cursor=MouseDropFolder(screen)==null?Cursors.No:Cursors.Hand;
        };
        files.GiveFeedback+=delegate{if((Control.MouseButtons&MouseButtons.Left)==0)HideDragBadge();else MoveDragBadge();};
        FormClosed+=delegate{dragTimer.Stop();dragTimer.Dispose();HideDragBadge();};




    }



    void ShowDragBadge(string text){if((Control.MouseButtons&MouseButtons.Left)==0){HideDragBadge();return;}if(dragBadge==null){dragBadge=new DragBadge();dragBadge.Show(this);}dragBadge.Message.Text=text;MoveDragBadge();dragBadge.Update();}
    void MoveDragBadge(){if(dragBadge!=null)dragBadge.Location=new Point(Cursor.Position.X+20,Cursor.Position.Y+22);}
    void HideDragBadge(){if(dragBadge!=null){dragBadge.Close();dragBadge.Dispose();dragBadge=null;}}

    void ShowInlineReport(string report){

        reportBox.Text=report;reportBox.Visible=true;

        int maximum=contentSplit.Height-contentSplit.Panel2MinSize-contentSplit.SplitterWidth;

        if(maximum>=contentSplit.Panel1MinSize)contentSplit.SplitterDistance=Math.Max(contentSplit.Panel1MinSize,Math.Min(maximum,contentSplit.Height-235));

    }

    async Task CheckDrive(){

        if(busy)return;var selected=drives.SelectedItem as Target;if(selected==null)return;bool reopen=session!=null;clipboard=null;

        if(session!=null){session.Dispose();session=null;}if(cancellation!=null)cancellation.Dispose();cancellation=new CancellationTokenSource();watch=Stopwatch.StartNew();Busy(true);bar.Style=ProgressBarStyle.Marquee;

        string reportPath=Path.Combine(Program.LogFolder,"fatx-check-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".txt");bool completed=false;

        try{

            string report=await Task.Run(()=>{using(var reader=new ExplorerDisk(selected,cancellation.Token,true)){return reader.CheckIntegrity((m,a,b)=>Progress(m,a,b,false));}});

            watch.Stop();report+=L.T("\r\nElapsed: ")+watch.Elapsed.ToString(@"hh\:mm\:ss");Directory.CreateDirectory(Program.LogFolder);File.WriteAllText(reportPath,report);completed=true;

            Program.SaveLog(L.T("Read-only FATX check saved to ")+reportPath);status.Text=L.T("Read-only check complete  -  report saved.");

            ShowInlineReport(report+L.T("\r\n\r\nSaved report: ")+reportPath);

        }catch(OperationCanceledException){ShowInlineReport(L.T("Check cancelled  -  inspection incomplete. No drive changes made."));status.Text=L.T("Check cancelled  -  no drive changes made.");Program.SaveLog(status.Text);}

        catch(Exception e){ShowInlineReport(L.T("Check incomplete: ")+e.Message+L.T("\r\nNo drive writes were performed."));Program.SaveLog(e.ToString());status.Text=L.T("Check incomplete  -  see report below.");try{Directory.CreateDirectory(Program.LogFolder);File.WriteAllText(reportPath,"RRFF read-only FATX check\r\nRESULT: INCOMPLETE\r\n"+e.Message+L.T("\r\nNo drive writes were performed."));}catch{}}

        finally{watch.Stop();Busy(false);bar.Style=ProgressBarStyle.Continuous;bar.Value=completed?100:0;}

        if(reopen){await Open();status.Text=completed?L.T("Check completed. Report saved to ")+reportPath:L.T("Check incomplete; see the reported error.");}

    }



    XboxEntry[] Selected(){return files.SelectedItems.Cast<ListViewItem>().Select(x=>(XboxEntry)x.Tag).ToArray();}

    void Busy(bool value){busy=value;backToChoices.Enabled=!value;check.Enabled=!value&&drives.SelectedItem!=null;drives.Enabled=refresh.Enabled=connect.Enabled=!value;partitions.Enabled=!value&&session!=null;files.Enabled=!value;foreach(var button in new[]{up,addFiles,addFolder,export,newFolder,delete,reload})button.Enabled=!value&&session!=null&&folders.Count>0;cancel.Enabled=value&&cancellation!=null;}

    async Task Scan(){clipboard=null;Busy(true);try{if(session!=null){session.Dispose();session=null;}partitions.Items.Clear();files.Items.Clear();folders.Clear();path.Text="";status.Text=L.T("Finding non-system drives...");var all=await Task.Run(()=>Storage.Scan());drives.Items.Clear();foreach(var d in all.Where(x=>!x.Protected))drives.Items.Add(d);drives.SelectedIndex=-1;status.Text=L.T("Choose a FATX HDD. Opening it does not format it.");}catch(Exception e){Error(e);}finally{Busy(false);}}

    async Task Open(){clipboard=null;var selected=drives.SelectedItem as Target;if(selected==null)return;Busy(true);try{if(session!=null)session.Dispose();session=null;target=selected;if(cancellation!=null)cancellation.Dispose();cancellation=new CancellationTokenSource();session=await Task.Run(()=>new ExplorerDisk(target,cancellation.Token));changing=true;partitions.Items.Clear();foreach(var p in session.Partitions)partitions.Items.Add(p);partitions.SelectedIndex=0;changing=false;}catch(Exception e){Error(e);}finally{changing=false;Busy(false);}if(session!=null)await ChangePartition();}

    async Task ChangePartition(){clipboard=null;var p=partitions.SelectedItem as XboxPartition;if(session==null||p==null)return;Busy(true);try{status.Text=L.T("Reading ")+p.Name+":\\...";folders.Clear();files.Items.Clear();await Task.Run(()=>session.Select(p));folders.Push(session.Root);long free=await Task.Run(()=>session.FreeBytes);path.Tag=L.T(" | Free ")+(free/1000000000.0).ToString("0.00")+" GB";}catch(Exception e){Error(e);}finally{Busy(false);}if(folders.Count>0)await ListFolder();}

    async Task ListFolder(){if(session==null||folders.Count==0)return;Busy(true);try{var entries=await Task.Run(()=>session.List(folders.Peek()));files.BeginUpdate();files.Items.Clear();foreach(var entry in entries.OrderByDescending(x=>x.Directory).ThenBy(x=>x.Name,StringComparer.OrdinalIgnoreCase)){var row=new ListViewItem(entry.Name){Tag=entry};row.SubItems.Add(entry.Directory?L.T("Folder"):L.T("File"));row.SubItems.Add(entry.Directory?"":entry.Size.ToString("N0")+L.T(" bytes"));files.Items.Add(row);}files.EndUpdate();path.Text=String.Join("\\",folders.Reverse().Select(x=>x.Name))+"\\";status.Text=entries.Count.ToString("N0")+L.T(" items")+(path.Tag as string??"")+L.T(". Existing names are not overwritten.");}catch(Exception e){Error(e);}finally{Busy(false);}}

    void Progress(string message,long done,long total,bool bytes){if((DateTime.UtcNow-lastProgress).TotalMilliseconds<200)return;lastProgress=DateTime.UtcNow;double elapsed=watch.Elapsed.TotalSeconds;BeginInvoke((Action)delegate{if(!busy)return;bar.Style=total<=0?ProgressBarStyle.Marquee:ProgressBarStyle.Continuous;bar.Value=total<=0?0:(int)Math.Min(99,done*100/total);status.Text=message+L.T("\nElapsed ")+watch.Elapsed.ToString(@"hh\:mm\:ss")+(total<=0?"":bytes?" | "+(done/1000000.0).ToString("0.0")+" / "+(total/1000000.0).ToString("0.0")+" MB | "+(elapsed>0?done*8/1000000.0/elapsed:0).ToString("0.0")+" Mbps":"")+(total<=0?"":" | "+bar.Value+"%");});}

    void ImportWithPatchList(XboxEntry destination,string[] sources,Action<string,long,long> progress){var consent=new GamePatchConsent(this);session.Import(destination,sources,progress,consent.Ask,consent.Confirm);}

    async Task Operation(string name,Action<Action<string,long,long>> action,bool bytes=true){if(session==null||busy)return;if(cancellation.IsCancellationRequested){MessageBox.Show(this,L.T("Reopen the HDD after cancelling before starting another operation."));return;}watch=Stopwatch.StartNew();lastProgress=DateTime.MinValue;status.Text=L.T(name)+L.T(" - preparing...");bar.Style=ProgressBarStyle.Marquee;Busy(true);bool success=false;string result="";try{Program.SaveLog(name+" started: "+path.Text);await Task.Run(()=>action((m,a,b)=>Progress(m,a,b,bytes)));success=true;result=L.T(name)+L.T(" completed. Total time: ")+watch.Elapsed.ToString(@"hh\:mm\:ss")+(name.StartsWith("Adding",StringComparison.Ordinal)?session.LastGameFixSummary:"");Program.SaveResult("SUCCESS",result);}catch(OperationCanceledException){result=L.T("Cancelled. Completed items may remain; reopen the HDD before continuing.");Program.SaveResult("CANCELLED",result);}catch(Exception e){result=L.T("Failed: ")+e.Message+L.T(" Completed items may remain.");Program.SaveResult("FAILED",e.ToString());MessageBox.Show(this,result,L.T("File operation failed"),MessageBoxButtons.OK,MessageBoxIcon.Error);}finally{watch.Stop();Busy(false);bar.Style=ProgressBarStyle.Continuous;bar.Value=success?100:0;}if(success){Busy(true);try{long free=await Task.Run(()=>session.FreeBytes);path.Tag=L.T(" | Free ")+(free/1000000000.0).ToString("0.00")+" GB";}finally{Busy(false);}await ListFolder();}status.Text=result;}

    void Error(Exception e){Program.SaveLog("Explorer error: "+e);status.Text=e.Message;MessageBox.Show(this,e.Message,L.T("FATX explorer"),MessageBoxButtons.OK,MessageBoxIcon.Error);}

    string AskName(){using(var dialog=new LocalizedForm{Text=L.T("New FATX folder"),ClientSize=new Size(470,125),StartPosition=FormStartPosition.CenterParent,FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false,MinimizeBox=false}){var text=new TextBox{Location=new Point(16,18),Width=438};var ok=new Button{Text=L.T("Create"),Location=new Point(260,65),DialogResult=DialogResult.OK};var no=new Button{Text=L.T("Cancel"),Location=new Point(360,65),DialogResult=DialogResult.Cancel};dialog.Controls.AddRange(new Control[]{text,ok,no});dialog.AcceptButton=ok;dialog.CancelButton=no;return dialog.ShowDialog(this)==DialogResult.OK?text.Text:null;}}

}

}

