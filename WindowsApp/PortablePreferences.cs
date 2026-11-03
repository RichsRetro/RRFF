using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace RichRetro.Fatx {
internal static class PortablePreferences {
    internal static readonly string Root=Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,".."));
    static readonly string InstalledRoot=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RichsRetro","RRFF");
    internal static readonly bool Portable=!String.Equals(Root.TrimEnd('\\'),InstalledRoot.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase)&&!File.Exists(Path.Combine(Root,"rrff-installed.mode"));
    static readonly string ConsentFile=Path.Combine(Root,"RRFF-preferences","consent.txt");
    internal static bool Accepted;
    static readonly Dictionary<char,string> Folders=new Dictionary<char,string>();
    internal static string DataFolder {get{return Path.Combine(Root,"RRFF-preferences");}}
    internal static bool CanSave {get{return !Portable||Accepted;}}
    internal static string LanguageFile {get{return Portable?Path.Combine(DataFolder,"language.txt"):Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RichsRetro","RRFFPreferences","language.txt");}}
    internal static void LoadConsent(){if(!Portable)return;try{Accepted=File.ReadAllText(ConsentFile).Trim()=="accepted-v1";}catch(IOException){}catch(UnauthorizedAccessException){}}
    internal static void Ask(){
        if(!Portable||Accepted)return;
        using(var form=new LocalizedForm()){
            form.Text=L.T("Portable app preferences");form.Font=new Font("Segoe UI",11);form.StartPosition=FormStartPosition.CenterScreen;
            form.Size=new Size(720,430);form.MinimumSize=form.Size;form.MaximizeBox=false;form.MinimizeBox=false;form.Padding=new Padding(20);
            var message=new TextBox{ReadOnly=true,Multiline=true,BorderStyle=BorderStyle.None,ScrollBars=ScrollBars.Vertical,BackColor=SystemColors.Control,TabStop=false,Dock=DockStyle.Fill,Text=L.T("RRFF Portable can remember previously used folder locations and your preferences in the folder it was launched from.\n\nAccept to save this information beside the app. Your acceptance will also be saved, so you will not be asked again.\n\nDecline to continue without saving preferences or folder history between launches.")};
            var buttons=new FlowLayoutPanel{Dock=DockStyle.Bottom,AutoSize=true,FlowDirection=FlowDirection.RightToLeft,Padding=new Padding(0,12,0,0)};
            var decline=new Button{Text=L.T("Decline"),DialogResult=DialogResult.No,AutoSize=true,MinimumSize=new Size(110,40)};
            var accept=new Button{Text=L.T("Accept"),DialogResult=DialogResult.Yes,AutoSize=true,MinimumSize=new Size(110,40)};
            buttons.Controls.Add(decline);buttons.Controls.Add(accept);form.Controls.Add(message);form.Controls.Add(buttons);form.CancelButton=decline;
            form.Shown+=delegate{decline.Focus();};
            if(form.ShowDialog()!=DialogResult.Yes)return;
        }
        try{Directory.CreateDirectory(DataFolder);File.WriteAllText(ConsentFile,"accepted-v1");Accepted=true;}
        catch(Exception e){if(!(e is IOException)&&!(e is UnauthorizedAccessException))throw;MessageBox.Show(L.T("Preferences could not be saved beside the app. RRFF will remember folders for this session only."),"RRFF");}
    }
    static string MemoryFile(char partition){return Portable?Path.Combine(DataFolder,"last-"+partition+"-folder.txt"):Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RichsRetro","RRXDFormatter","last-"+partition+"-folder.txt");}
    internal static string ReadFolder(char partition){lock(Folders){string value;if(Folders.TryGetValue(partition,out value))return value;}if(!CanSave)return null;try{return File.ReadAllText(MemoryFile(partition)).Trim();}catch(IOException){}catch(UnauthorizedAccessException){}return null;}
    internal static void SaveFolder(char partition,string value){lock(Folders){Folders[partition]=value;}if(!CanSave)return;try{string file=MemoryFile(partition);Directory.CreateDirectory(Path.GetDirectoryName(file));File.WriteAllText(file,value);}catch(IOException){}catch(UnauthorizedAccessException){}}
}
}
