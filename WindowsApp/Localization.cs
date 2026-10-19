using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using System.Drawing;
namespace RichRetro.Fatx {
internal static class L {
 internal static readonly string[] Codes={"auto","en","de","es","fr","it","pt"};
 internal static readonly string[] Names={"System default","English","Deutsch","Español","Français","Italiano","Português"};
 static readonly string PreferencePath=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RichsRetro","RRFFPreferences","language.txt");
 static readonly Dictionary<string,string[]> Rows=Load();
 internal static string Preference="auto";
 static string language="en";
 internal static string Language {get{return language;}}
 static Dictionary<string,string[]> Load(){using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("translations.json"))using(var reader=new StreamReader(stream,Encoding.UTF8))return new JavaScriptSerializer().Deserialize<Dictionary<string,string[]>>(reader.ReadToEnd());}
 internal static void Initialize(){string pref="auto";try{if(File.Exists(PreferencePath))pref=File.ReadAllText(PreferencePath).Trim();}catch{}Set(pref,false);}
 internal static bool Set(string pref,bool save){if(Array.IndexOf(Codes,pref)<0)pref="auto";Preference=pref;language=pref=="auto"?CultureInfo.CurrentUICulture.TwoLetterISOLanguageName:pref;if(Array.IndexOf(Codes,language)<1)language="en";if(save){try{Directory.CreateDirectory(Path.GetDirectoryName(PreferencePath));File.WriteAllText(PreferencePath,pref,Encoding.UTF8);}catch(Exception e){Program.SaveLog("Language preference could not be saved: "+e.Message);return false;}}return true;}
 internal static string T(string value){if(String.IsNullOrEmpty(value))return value;string key=value;string[] row;bool trimmed=false;if(!Rows.TryGetValue(key,out row)){key=value.Trim();trimmed=true;if(!Rows.TryGetValue(key,out row))return value;}int index=Array.IndexOf(Codes,language)-1;string result=index<=0?key:row[index-1];if(trimmed){int start=value.IndexOf(key,StringComparison.Ordinal);return value.Substring(0,start)+result+value.Substring(start+key.Length);}return result;}
 internal static void Apply(Control root){foreach(Control child in root.Controls)Apply(child);if(root is TextBoxBase && !((TextBoxBase)root).ReadOnly)return;if(root is TextBoxBase)return;root.Text=T(root.Text);root.TextChanged+=delegate{string translated=T(root.Text);if(translated!=root.Text)root.Text=translated;};root.ControlAdded+=delegate(object sender,ControlEventArgs e){Apply(e.Control);};var list=root as ListView;if(list!=null)foreach(ColumnHeader column in list.Columns)column.Text=T(column.Text);var strip=root as ToolStrip;if(strip!=null)foreach(ToolStripItem item in strip.Items)ApplyItem(item);}
 static void ApplyItem(ToolStripItem item){item.Text=T(item.Text);var menu=item as ToolStripDropDownItem;if(menu!=null)foreach(ToolStripItem child in menu.DropDownItems)ApplyItem(child);}
}
internal class LocalizedForm:Form {protected override void OnLoad(EventArgs e){L.Apply(this);base.OnLoad(e);}}
internal static class MessageBox {
 internal static DialogResult Show(string text){return Show(null,text,"RRFF");}
 internal static DialogResult Show(string text,string caption,MessageBoxButtons buttons=MessageBoxButtons.OK,MessageBoxIcon icon=MessageBoxIcon.None,MessageBoxDefaultButton defaultButton=MessageBoxDefaultButton.Button1){return Show(null,text,caption,buttons,icon,defaultButton);}
 internal static DialogResult Show(IWin32Window owner,string text,string caption="RRFF",MessageBoxButtons buttons=MessageBoxButtons.OK,MessageBoxIcon icon=MessageBoxIcon.None,MessageBoxDefaultButton defaultButton=MessageBoxDefaultButton.Button1){
 using(var dialog=new LocalizedForm()){dialog.Text=L.T(caption);dialog.StartPosition=owner==null?FormStartPosition.CenterScreen:FormStartPosition.CenterParent;dialog.Size=new Size(700,430);dialog.MinimumSize=new Size(520,300);dialog.Font=new Font("Segoe UI",11);dialog.MinimizeBox=false;dialog.MaximizeBox=false;dialog.ShowInTaskbar=false;
 var message=new TextBox{Text=L.T(text),ReadOnly=true,Multiline=true,BorderStyle=BorderStyle.None,ScrollBars=ScrollBars.Vertical,Dock=DockStyle.Fill,BackColor=SystemColors.Control,TabStop=false};
 var panel=new FlowLayoutPanel{Dock=DockStyle.Bottom,AutoSize=true,FlowDirection=FlowDirection.RightToLeft,Padding=new Padding(12),WrapContents=false};dialog.Padding=new Padding(18);dialog.Controls.Add(message);dialog.Controls.Add(panel);
 DialogResult[] choices=buttons==MessageBoxButtons.YesNo?new[]{DialogResult.Yes,DialogResult.No}:buttons==MessageBoxButtons.YesNoCancel?new[]{DialogResult.Yes,DialogResult.No,DialogResult.Cancel}:buttons==MessageBoxButtons.OKCancel?new[]{DialogResult.OK,DialogResult.Cancel}:buttons==MessageBoxButtons.RetryCancel?new[]{DialogResult.Retry,DialogResult.Cancel}:buttons==MessageBoxButtons.AbortRetryIgnore?new[]{DialogResult.Abort,DialogResult.Retry,DialogResult.Ignore}:new[]{DialogResult.OK};
 var controls=new List<Button>();for(int i=choices.Length-1;i>=0;i--){var button=new Button{Text=L.T(choices[i].ToString()),DialogResult=choices[i],AutoSize=true,MinimumSize=new Size(100,38)};panel.Controls.Add(button);controls.Insert(0,button);if(choices[i]==DialogResult.Cancel||choices[i]==DialogResult.No)dialog.CancelButton=button;}
 int focus=Math.Min(controls.Count-1,defaultButton==MessageBoxDefaultButton.Button3?2:defaultButton==MessageBoxDefaultButton.Button2?1:0);dialog.AcceptButton=controls[focus];dialog.Shown+=delegate{controls[focus].Focus();};if(icon!=MessageBoxIcon.None){var image=new PictureBox{Width=42,Dock=DockStyle.Left,Image=(icon==MessageBoxIcon.Warning?SystemIcons.Warning:icon==MessageBoxIcon.Error?SystemIcons.Error:icon==MessageBoxIcon.Question?SystemIcons.Question:SystemIcons.Information).ToBitmap()};dialog.Controls.Add(image);}return owner==null?dialog.ShowDialog():dialog.ShowDialog(owner);}
 }
}
}
