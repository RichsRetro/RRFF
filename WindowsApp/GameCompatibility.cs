using System;

using System.Collections;

using System.Collections.Generic;

using System.IO;

using System.IO.Compression;

using System.Linq;

using System.Reflection;

using System.Security.Cryptography;

using System.Web.Script.Serialization;

using System.Windows.Forms;

using System.Threading;

namespace RichRetro.Fatx {

// Independent destination-copy patcher. No ACL/JMP scripts or downloaded code execute.

internal sealed class GameCompatibility {

    static readonly Dictionary<string,Dictionary<string,object>> Profiles=LoadProfiles();

    public readonly string Root,Description;

    public string FolderName;

    public XisoImage Image;public byte[] ImageLauncher;public bool KeepImage;

    public string HardwareWarning;

    public bool HasChanges {get{return Image!=null||KeepImage||renames.Count>0||omissions.Count>0||replacements.Count>0;}}

    readonly List<Tuple<string,string>> renames=new List<Tuple<string,string>>();

    readonly Dictionary<string,string> omissions=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);

    readonly Dictionary<string,byte[]> replacements=new Dictionary<string,byte[]>(StringComparer.OrdinalIgnoreCase);

    GameCompatibility(string root,string label){Root=root.TrimEnd('\\');Description=label.Replace(L.T(" (hardware test pending)"),"");}

    static Dictionary<string,Dictionary<string,object>> LoadProfiles(){

        var result=new Dictionary<string,Dictionary<string,object>>(StringComparer.OrdinalIgnoreCase);

        var assembly=Assembly.GetExecutingAssembly();

        foreach(string name in assembly.GetManifestResourceNames().Where(n=>n.StartsWith("game-patch-",StringComparison.Ordinal)))

            using(var stream=assembly.GetManifestResourceStream(name))using(var reader=new StreamReader(stream)){

                var profile=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(reader.ReadToEnd());result.Add((string)profile["title_id"],profile);

            }

        return result;

    }

    static string Hash(byte[] bytes){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();}

    static byte[] Hex(string value){var bytes=new byte[value.Length/2];for(int i=0;i<bytes.Length;i++)bytes[i]=Convert.ToByte(value.Substring(i*2,2),16);return bytes;}

    static IEnumerable<Dictionary<string,object>> Records(object records){return ((IEnumerable)records).Cast<object>().Select(x=>(Dictionary<string,object>)x);}

    static byte[] ReadTarget(string path){var file=new FileInfo(path);if((file.Attributes&FileAttributes.ReparsePoint)!=0||file.Length>64*1024*1024)throw new IOException(L.T("Unsupported compatibility-patch target: ")+path);return File.ReadAllBytes(path);}

    public static GameCompatibility ForFolder(string folder,Func<GameCompatibility,bool> approval=null,CancellationToken cancellation=default(CancellationToken)){

        string executable=Path.Combine(folder,"default.xbe");if(!File.Exists(executable))return null;
        using(var input=new FileStream(executable,FileMode.Open,FileAccess.Read,FileShare.Read)){
            var header=new byte[0x11c];if(input.Read(header,0,header.Length)!=header.Length||BitConverter.ToUInt32(header,0)!=0x48454258)return null;
            long cert=(long)BitConverter.ToUInt32(header,0x118)-BitConverter.ToUInt32(header,0x104);if(cert<0||cert>input.Length-12)return null;
            input.Position=cert+8;var id=new byte[4];if(input.Read(id,0,4)!=4||!Profiles.ContainsKey(BitConverter.ToUInt32(id,0).ToString("x8")))return null;
        }
        return ForSource(folder,r=>File.Exists(Path.Combine(folder,r.Replace('/','\\'))),r=>ReadTarget(Path.Combine(folder,r.Replace('/','\\'))),()=>new XisoImage(folder,cancellation),false,approval,cancellation);

    }

    public static GameCompatibility ForImage(XisoReader image,Func<GameCompatibility,bool> approval,CancellationToken cancellation){

        if(!Profiles.ContainsKey(image.TitleId.ToString("x8")))return null;
        return ForSource(image.Key,image.Exists,image.SmallFile,null,true,approval,cancellation);

    }

    static GameCompatibility ForSource(string folder,Func<string,bool> exists,Func<string,byte[]> read,Func<XisoImage> makeImage,bool fromImage,Func<GameCompatibility,bool> approval,CancellationToken cancellation){

        cancellation.ThrowIfCancellationRequested();if(!exists("default.xbe"))return null;

        var data=read("default.xbe");string id;

        if(data.Length<0x11c||BitConverter.ToUInt32(data,0)!=0x48454258)return null;

        uint imageBase=BitConverter.ToUInt32(data,0x104),cert=BitConverter.ToUInt32(data,0x118);

        if(cert<imageBase||(long)(cert-imageBase)+12>data.Length)return null;

        id=BitConverter.ToUInt32(data,(int)(cert-imageBase)+8).ToString("x8");

        Dictionary<string,object> profile;if(!Profiles.TryGetValue(id,out profile))return null;string fingerprint=Hash(data);

        var version=Records(profile["versions"]).SingleOrDefault(v=>(string)v["default_xbe_sha256"]==fingerprint||(v.ContainsKey("patched_xbe_sha256")&&(string)v["patched_xbe_sha256"]==fingerprint));if(version==null)return null;

        var plan=new GameCompatibility(folder,fromImage?((string)version["label"]).Replace(L.T("convert folder to XISO"),L.T("retain XISO")):((string)version["label"]));

        object warning;if(version.TryGetValue("hardware_warning",out warning))plan.HardwareWarning=(string)warning;

        object folderName;if(version.TryGetValue("folder_name",out folderName))plan.FolderName=(string)folderName;

        object imageMode;if(version.TryGetValue("install_as_xiso",out imageMode)&&(bool)imageMode){

            if(fromImage)plan.KeepImage=true;else plan.Image=makeImage();plan.ImageLauncher=XisoImage.Launcher(Convert.ToUInt32(id,16),plan.FolderName??(string)version["label"]);

            if(approval!=null&&!approval(plan))throw new OperationCanceledException(L.T("Game conversion declined; no HDD files changed."));return plan;

        }

        var targets=Records(version[L.T("files")]).ToList();

        if(targets.Count>0&&targets.All(t=>t.ContainsKey("patched_sha256")&&Hash(read((string)t["path"]))==(string)t["patched_sha256"])&&Records(version["renames"]).All(r=>exists((string)r["to_prefix"])&&!exists((string)r["from_prefix"])))return new GameCompatibility(folder,((string)version["label"])+"  -  already patched"){FolderName=plan.FolderName};

        foreach(var rename in Records(version["renames"]))plan.renames.Add(Tuple.Create((string)rename["from_prefix"],(string)rename["to_prefix"]));

        foreach(var file in Records(version[L.T("files")])){string relative=(string)file["path"];var original=read(relative);if(Hash(original)!=(string)file["sha256"])throw new IOException(L.T("Compatibility target does not match its fingerprint."));

            using(var output=new MemoryStream()){int cursor=0;

                foreach(var edit in Records(file["patches"]).OrderBy(e=>Convert.ToInt32(e["offset"]))){int offset=Convert.ToInt32(edit["offset"]);byte[] before=Hex((string)edit["before"]),after=Hex((string)edit[L.T("after")]);if(offset<cursor||offset>original.Length-before.Length||!original.Skip(offset).Take(before.Length).SequenceEqual(before))throw new IOException(L.T("Expected game patch bytes do not match."));output.Write(original,cursor,offset-cursor);output.Write(after,0,after.Length);cursor=offset+before.Length;}

                output.Write(original,cursor,original.Length-cursor);var patched=output.ToArray();object expected;if(file.TryGetValue("patched_sha256",out expected)&&Hash(patched)!=(string)expected)throw new IOException(L.T("Generated game patch fingerprint does not match."));plan.replacements.Add(relative,patched);

            }

        }

        object omit;if(version.TryGetValue("exclude_build_logs",out omit))foreach(var file in Records(omit)){string relative=(string)file["path"],expected=(string)file["sha256"],path=Path.Combine(folder,relative.Replace('/','\\'));if(exists(relative)&&Hash(read(relative))!=expected)throw new IOException(L.T("Build-log exclusion fingerprint does not match."));plan.omissions.Add(relative,expected);}

        if(plan.HasChanges&&approval!=null&&!approval(plan))throw new OperationCanceledException(L.T("Game patch declined; no HDD files changed."));

        return plan;

    }

    string Relative(string source){if(!source.StartsWith(Root+"\\",StringComparison.OrdinalIgnoreCase))throw new IOException(L.T("Source outside game patch root."));return source.Substring(Root.Length+1).Replace('\\','/');}

    public bool Skip(string source){return omissions.ContainsKey(Relative(source));}

    public string Name(string source){string relative=Relative(source);foreach(var rename in renames)if(relative.StartsWith(rename.Item1,StringComparison.OrdinalIgnoreCase)){relative=rename.Item2+relative.Substring(rename.Item1.Length);break;}return relative.Substring(relative.LastIndexOf('/')+1);}

    public byte[] Replacement(string source){byte[] bytes;return replacements.TryGetValue(Relative(source),out bytes)?bytes:null;}

    public static string KnownCases(){var assembly=Assembly.GetExecutingAssembly();string resource=L.Language=="en"?"game-compatibility.md":"game-compatibility."+L.Language+".md";using(var stream=assembly.GetManifestResourceStream(resource)??assembly.GetManifestResourceStream("game-compatibility.md"))using(var reader=new StreamReader(stream,System.Text.Encoding.UTF8))return reader.ReadToEnd();}

}

// Collect during source planning; approve the complete list before any HDD writes.

internal sealed class GamePatchConsent {

    readonly Form owner;readonly Dictionary<string,GameCompatibility> games=new Dictionary<string,GameCompatibility>(StringComparer.OrdinalIgnoreCase);

    public GamePatchConsent(Form form){owner=form;}

    public bool Ask(GameCompatibility game){games[game.Root]=game;return true;}

    public void Confirm(){

        if(games.Count==0)return;

        Func<bool> show=()=>{

            using(var dialog=new Form{Text=L.T("Game HDD compatibility fixes"),Size=new System.Drawing.Size(780,550),MinimumSize=new System.Drawing.Size(620,400),StartPosition=FormStartPosition.CenterParent,MinimizeBox=false,MaximizeBox=false,Font=new System.Drawing.Font("Segoe UI",11),BackColor=System.Drawing.Color.FromArgb(12,20,30),ForeColor=System.Drawing.Color.FromArgb(220,230,235)}){

                var heading=new Label{Text=L.T("These games need compatibility fixes. Apply them to the HDD copies?\r\nYour Windows originals stay unchanged."),Dock=DockStyle.Top,Height=68,Padding=new Padding(12)};

                var list=new TextBox{Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical,Dock=DockStyle.Fill,BackColor=dialog.BackColor,ForeColor=dialog.ForeColor};

                list.Text=String.Join("\r\n\r\n",games.Values.OrderBy(g=>g.Description).Select(g=>g.Description+(String.IsNullOrEmpty(g.HardwareWarning)?"":"\r\n"+g.HardwareWarning)));

                var buttons=new FlowLayoutPanel{Dock=DockStyle.Bottom,Height=58,FlowDirection=FlowDirection.RightToLeft,Padding=new Padding(10)};

                var cancel=new Button{Text=L.T("Cancel"),AutoSize=true,DialogResult=DialogResult.Cancel};var apply=new Button{Text=L.T("Apply all fixes"),AutoSize=true,DialogResult=DialogResult.OK};

                buttons.Controls.Add(cancel);buttons.Controls.Add(apply);dialog.Controls.Add(list);dialog.Controls.Add(heading);dialog.Controls.Add(buttons);dialog.AcceptButton=apply;dialog.CancelButton=cancel;

                return dialog.ShowDialog(owner)==DialogResult.OK;

            }

        };

        bool approved=owner.InvokeRequired?(bool)owner.Invoke(show):show();

        if(!approved)throw new OperationCanceledException(L.T("Game patches declined; no HDD files changed."));

    }

}

}

