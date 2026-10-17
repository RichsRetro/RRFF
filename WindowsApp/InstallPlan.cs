using System;

using System.Drawing;

using System.IO;

using System.Windows.Forms;



namespace RichRetro.Fatx {

internal sealed class InstallPlan {

    string SourceSummary(){

        string text="";string[] sources={CFolder,EFolder,FFolder,GFolder};

        for(int i=0;i<sources.Length;i++)if(sources[i]!=null)text+="\nContents of "+sources[i]+"  ->  "+(i==2&&AutoSplit?"F/G":"CEFG"[i].ToString())+":\\";

        if(FPrefix.Length>0)text+="\nF source contents -> F:\\"+FPrefix;if(GPrefix.Length>0)text+="\nG source contents -> G:\\"+GPrefix;if(FLooseGames)text+="\nLoose F executable folders -> F:\\Games";if(GLooseGames)text+="\nLoose G executable folders -> G:\\Games";
        if(FImages.Length>0)text+="\n"+FImages.Length+" XISO image(s) -> "+(AutoSplit?"F/G":"F")+" (extracted or retained with a launcher as appropriate)";if(GImages.Length>0)text+="\n"+GImages.Length+" XISO image(s) -> G";
        return text.Length==0?"":text+((CFolder!=null||EFolder!=null||FFolder!=null||GFolder!=null)?"\nThe selected folder itself is NOT copied. To keep Games or Apps, select its parent folder.":"\nSelected images are installed into Games using clean game-folder names.");

    }

    public bool Rrxd=true,SkipCConflicts,FullVerify; public bool SplitFG,AutoSplit; public string CFolder,EFolder,FFolder,GFolder;
    public string FPrefix="",GPrefix="";public bool FLooseGames,GLooseGames;
    public string[] FImages=new string[0],GImages=new string[0];

    public string Summary { get { return "C: "+(Rrxd?"RRXD" : "custom")+(Rrxd && CFolder!=null?" + your files":"")+"\nE: "+(EFolder==null?"default folders":"your folder")+"\nF: "+(FImages.Length>0?FImages.Length+" XISO image(s)":FFolder==null?"Apps + Games":"your folder")+(SplitFG?"\nG: "+(AutoSplit?"automatic overflow from F":GImages.Length>0?GImages.Length+" XISO image(s)":GFolder==null?"Apps + Games":"your folder"):"")+SourceSummary()+"\nVerification: "+(FullVerify?"full library read-back":"quick  -  structure and C/E checked"); } }

}

internal sealed class SplitLayoutDialog:Form {

    public SplitLayoutDialog(long bytes){

        Text="Large Xbox drive  -  F and G";ClientSize=new Size(800,260);StartPosition=FormStartPosition.CenterParent;FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;MinimizeBox=false;Font=new Font("Segoe UI",11);BackColor=Color.FromArgb(12,20,30);ForeColor=Color.FromArgb(220,230,235);

        double g=(bytes/512-Fatx.FStart-Fatx.MaxExtendedSectors)*512/1000000000.0;

        Controls.Add(new Label{Dock=DockStyle.Fill,Padding=new Padding(20),Text="This drive is larger than the single-F limit.\n\nUse F up to 1 TiB (1,099.51 GB), and give G the remaining "+g.ToString("N2")+" GB? Usable space is slightly lower after formatting.\n\nAutomatically fill F then continue on G, keeping whole games together, or choose separate F and G source folders. The plan is checked before any erase."});

        var row=new FlowLayoutPanel{Dock=DockStyle.Bottom,Height=66,Padding=new Padding(16)};

        var auto=new Button{Text="Automatically fill F, then G",AutoSize=true,DialogResult=DialogResult.Yes};var manual=new Button{Text="Choose F and G folders",AutoSize=true,DialogResult=DialogResult.No};var stop=new Button{Text="Cancel",AutoSize=true,DialogResult=DialogResult.Cancel};row.Controls.AddRange(new Control[]{auto,manual,stop});Controls.Add(row);CancelButton=stop;

    }

}

internal sealed class CConflictException:IOException {

    public readonly string RelativePath;

    public CConflictException(string path):base("Custom C conflicts with RRXD: "+path){RelativePath=path;}

}

internal sealed class CConflictDialog:Form {

    public CConflictDialog(string path){

        Text="Choose which C files to use";ClientSize=new Size(780,240);FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;MinimizeBox=false;StartPosition=FormStartPosition.CenterParent;Font=new Font("Segoe UI",11);BackColor=Color.FromArgb(12,20,30);ForeColor=Color.FromArgb(220,230,235);

        var text=new Label{Text="Your C folder has files that conflict with RRXD.\n\nWould you like to skip the conflicting files from your C folder and use RRXD, or continue with your own C folder without RRXD?",Dock=DockStyle.Top,Height=126,Padding=new Padding(18)};Controls.Add(text);

        var item=new Label{Text="First conflict: C:\\"+path,AutoEllipsis=true,Dock=DockStyle.Top,Height=35,Padding=new Padding(18,0,18,0)};Controls.Add(item);item.BringToFront();text.BringToFront();

        var row=new FlowLayoutPanel{Dock=DockStyle.Bottom,Height=63,Padding=new Padding(12)};

        var keep=new Button{Text="Use RRXD  -  skip conflicts",AutoSize=true,DialogResult=DialogResult.Yes};var own=new Button{Text="Use my C  -  without RRXD",AutoSize=true,DialogResult=DialogResult.No};var stop=new Button{Text="Cancel",AutoSize=true,DialogResult=DialogResult.Cancel};

        row.Controls.AddRange(new Control[]{keep,own,stop});Controls.Add(row);CancelButton=stop;AcceptButton=keep;

    }

}

internal sealed class SetupWizard:Form {

    readonly CheckBox rrxd=new CheckBox{Text="Install RRXD dashboard",Checked=true,AutoSize=true};

    readonly CheckBox extra=new CheckBox{Text="Add my own files to C as well",AutoSize=true};

    readonly CheckBox custom=new CheckBox{Text="Use my own folder",AutoSize=true};

    readonly CheckBox fullVerify=new CheckBox{Text="Also read back every game file (slower)",AutoSize=true};

    readonly Label heading=new Label{Dock=DockStyle.Fill,Font=new Font("Segoe UI",19,FontStyle.Bold),ForeColor=Color.Cyan};

    readonly Label explanation=new Label{Dock=DockStyle.Fill};

    readonly TextBox folder=new TextBox{Dock=DockStyle.Fill,ReadOnly=true};

    readonly Button browse=new Button{Text="Choose folder...",AutoSize=true};

    readonly Button images=new Button{Text="Choose XISO files...",AutoSize=true};
    readonly Button next=new Button{Text="Next",AutoSize=true};

    readonly Button back=new Button{Text="Back",AutoSize=true};

    readonly FlowLayoutPanel choices=new FlowLayoutPanel{Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown};

    int page;readonly InstallPlan plan=new InstallPlan();bool updating;readonly int pages;

    public InstallPlan Plan {get{return plan;}}

    public SetupWizard(bool split=false,bool automatic=false) {

        plan.SplitFG=split;plan.AutoSplit=automatic;pages=split&&!automatic?4:3;

        Text="Choose what goes on your Xbox drive";Size=new Size(850,500);MinimumSize=Size;StartPosition=FormStartPosition.CenterParent;Font=new Font("Segoe UI",11);BackColor=Color.FromArgb(12,20,30);ForeColor=Color.FromArgb(220,230,235);

        var root=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(20),ColumnCount=1,RowCount=6};Controls.Add(root);

        foreach(int h in new[]{48,125,72,38,40,50})root.RowStyles.Add(new RowStyle(SizeType.Absolute,h));

        root.Controls.Add(heading,0,0);root.Controls.Add(explanation,0,1);root.Controls.Add(choices,0,2);root.Controls.Add(folder,0,3);var sourceButtons=new FlowLayoutPanel{Dock=DockStyle.Fill};sourceButtons.Controls.AddRange(new Control[]{browse,images});root.Controls.Add(sourceButtons,0,4);

        var buttons=new FlowLayoutPanel{Dock=DockStyle.Fill,FlowDirection=FlowDirection.RightToLeft};var close=new Button{Text="Cancel",AutoSize=true,DialogResult=DialogResult.Cancel};buttons.Controls.AddRange(new Control[]{next,back,close});root.Controls.Add(buttons,0,5);

        rrxd.CheckedChanged+=delegate{if(!updating)ShowPage();};extra.CheckedChanged+=delegate{if(!updating)UpdateFolder();};custom.CheckedChanged+=delegate{if(!updating)UpdateFolder();};

        browse.Click+=delegate{try{string selected=FolderPicker.Choose(this,"CEFG"[page],folder.Text,true);if(selected!=null){if(page==2)plan.FImages=new string[0];if(page==3)plan.GImages=new string[0];folder.Text=selected;SaveFolder();}}catch(Exception e){MessageBox.Show(this,"Could not open the folder picker: "+e.Message);}};

        images.Click+=delegate{using(var dialog=new OpenFileDialog{Title="Select Xbox XISO images",Filter="Xbox XISO images (*.iso;*.xiso)|*.iso;*.xiso",Multiselect=true,CheckFileExists=true}){if(dialog.ShowDialog(this)!=DialogResult.OK)return;if(page==2){plan.FImages=dialog.FileNames;plan.FFolder=null;}else{plan.GImages=dialog.FileNames;plan.GFolder=null;}custom.Checked=true;folder.Text=dialog.FileNames.Length+" XISO image(s) selected";}};
        next.Click+=delegate{if(!SavePage())return;if(page==pages-1){DialogResult=DialogResult.OK;Close();}else{page++;ShowPage();}};

        back.Click+=delegate{SavePage(false);page--;ShowPage();};ShowPage();

    }

    bool WantsFolder {get{return page==0?(!rrxd.Checked||extra.Checked):custom.Checked;}}

    void SaveFolder(){if(page==0)plan.CFolder=folder.Text;else if(page==1)plan.EFolder=folder.Text;else if(page==2)plan.FFolder=folder.Text;else plan.GFolder=folder.Text;}

    bool SavePage(bool validate=true){

        if(validate && WantsFolder && !((page==2&&plan.FImages.Length>0)||(page==3&&plan.GImages.Length>0)) && !Directory.Exists(folder.Text)){MessageBox.Show(this,"Choose an existing folder first.");return false;}

        if(page==0){plan.Rrxd=rrxd.Checked;plan.CFolder=WantsFolder?folder.Text:null;}else if(page==1)plan.EFolder=WantsFolder?folder.Text:null;else if(page==2){plan.FFolder=WantsFolder&&plan.FImages.Length==0?folder.Text:null;if(!WantsFolder)plan.FImages=new string[0];plan.FullVerify=fullVerify.Checked;}else {plan.GFolder=WantsFolder&&plan.GImages.Length==0?folder.Text:null;if(!WantsFolder)plan.GImages=new string[0];}return true;

    }

    void UpdateFolder(){folder.Enabled=WantsFolder;browse.Enabled=WantsFolder;}

    void ShowPage(){updating=true;choices.Controls.Clear();heading.Text=(page+1)+" / "+pages+"  -  "+"CEFG"[page]+" partition";back.Enabled=page>0;next.Text=page==pages-1?"Check files":"Next";

        if(page==0){choices.Controls.Add(rrxd);if(rrxd.Checked)choices.Controls.Add(extra);explanation.Text="Install RRXD or use your own C files. Only the CONTENTS of your selected folder go into C:\\  -  the selected folder itself is not copied. Conflicts let you keep RRXD or use your own C.";folder.Text=plan.CFolder??"";}

        else{choices.Controls.Add(custom);if(page==2){fullVerify.Checked=plan.FullVerify;choices.Controls.Add(fullVerify);}custom.Checked=(page==1?plan.EFolder:page==2?plan.FFolder:plan.GFolder)!=null||(page==2&&plan.FImages.Length>0)||(page==3&&plan.GImages.Length>0);folder.Text=(page==1?plan.EFolder:page==2?plan.FFolder:plan.GFolder)??"";explanation.Text=page==1?"Only the CONTENTS of your selected folder go into E:\\  -  the selected folder itself is not copied. To keep an Apps folder, select its parent folder.":"Only the CONTENTS of your selected folder go into F:\\  -  the selected folder itself is not copied. Select the parent of Games to get F:\\Games; selecting Games puts the game folders directly in F:\\.";}

        if(page==2&&plan.AutoSplit)explanation.Text="Select the folder whose CONTENTS should be installed across F and G. F fills first, then G. Complete game/app folders stay together; no individual file is split.";

        if(page==3)explanation.Text="Only the CONTENTS of this folder go into G:\\  -  the selected folder itself is not copied. Choose the parent of Games to retain G:\\Games. Default creates empty Apps and Games folders.";

        if(page>=2)explanation.Text+=" Select XISO files below instead to extract games directly, apply recognised fixes, or retain supported image-only games with a launcher. Windows originals stay unchanged.";
        images.Visible=page>=2;if(page==2&&plan.FImages.Length>0)folder.Text=plan.FImages.Length+" XISO image(s) selected";if(page==3&&plan.GImages.Length>0)folder.Text=plan.GImages.Length+" XISO image(s) selected";
        updating=false;UpdateFolder();

    }

}

}

