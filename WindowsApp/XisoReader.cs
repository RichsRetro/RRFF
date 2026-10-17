using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace RichRetro.Fatx {
// Reads plain XDVDFS images without extracting a temporary Windows game tree.
internal sealed class XisoReader {
    internal sealed class Entry {
        public string Name,Relative; public bool Directory; public long Offset,Size;
        public readonly List<Entry> Children=new List<Entry>();
    }
    readonly string image; readonly long length; readonly DateTime stamp;
    readonly CancellationToken cancel;
    readonly Dictionary<string,Entry> files=new Dictionary<string,Entry>(StringComparer.OrdinalIgnoreCase);
    readonly HashSet<long> directories=new HashSet<long>(); int entries;
    public uint TitleId {get;private set;}string title;
    public readonly Entry Root=new Entry{Directory=true,Relative="",Name=""};
    public string Key {get{return image+".rrxd-contents";}}
    public long Length {get{return length;}}
    public static bool Candidate(string path){return path.EndsWith(".iso",StringComparison.OrdinalIgnoreCase)||path.EndsWith(".xiso",StringComparison.OrdinalIgnoreCase);}
    public XisoReader(string path,CancellationToken token){
        image=Path.GetFullPath(path);cancel=token;var info=new FileInfo(image);
        if((info.Attributes&FileAttributes.ReparsePoint)!=0)throw new IOException("Image links are not supported.");
        length=info.Length;stamp=info.LastWriteTimeUtc;
        using(var input=OpenContainer()){
            byte[] descriptor=Read(input,65536,2048);
            byte[] magic=Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA");
            if(!descriptor.Take(20).SequenceEqual(magic)||!descriptor.Skip(2028).Take(20).SequenceEqual(magic))
                throw new IOException("Not a plain Xbox XISO image: "+Path.GetFileName(path)+". Compressed images and full-disc dumps must be converted to XISO first.");
            try{ReadDirectory(input,Root,BitConverter.ToUInt32(descriptor,20),BitConverter.ToUInt32(descriptor,24),0);}catch(IOException e){throw new IOException("Cannot read "+Path.GetFileName(image)+": "+e.Message,e);}
        }
        if(!files.ContainsKey("default.xbe"))throw new IOException("Xbox image has no root default.xbe: "+Path.GetFileName(image));
        var executable=files["default.xbe"];if(executable.Size<0x11c)throw new IOException("Image executable is truncated.");
        using(var input=OpenContainer()){
            var header=Read(input,executable.Offset,0x11c);if(Encoding.ASCII.GetString(header,0,4)!="XBEH")throw new IOException("Image default.xbe is not an Xbox executable.");
            long cert=(long)BitConverter.ToUInt32(header,0x118)-BitConverter.ToUInt32(header,0x104);
            if(cert<0||cert>executable.Size-92)throw new IOException("Image executable certificate is outside the file.");
            var certificate=Read(input,executable.Offset+cert,92);TitleId=BitConverter.ToUInt32(certificate,8);title=Encoding.Unicode.GetString(certificate,12,80).TrimEnd('\0').Trim();
        }
    }
    void Bounds(long offset,long size){if(offset<0||size<0||offset>length-size)throw new IOException("XISO entry is outside the image.");}
    FileStream OpenContainer(){
        cancel.ThrowIfCancellationRequested();var info=new FileInfo(image);
        if(!info.Exists||info.Length!=length||info.LastWriteTimeUtc!=stamp||(info.Attributes&FileAttributes.ReparsePoint)!=0)throw new IOException("Source image changed: "+image);
        var stream=new FileStream(image,FileMode.Open,FileAccess.Read,FileShare.Read,65536,FileOptions.RandomAccess);
        if(stream.Length!=length){stream.Dispose();throw new IOException("Source image size changed.");}return stream;
    }
    byte[] Read(FileStream input,long offset,int size){Bounds(offset,size);cancel.ThrowIfCancellationRequested();var bytes=new byte[size];input.Position=offset;int at=0;while(at<size){int got=input.Read(bytes,at,size-at);if(got==0)throw new IOException("Truncated XISO image.");at+=got;}return bytes;}
    void ReadDirectory(FileStream input,Entry parent,uint sector,uint size,int depth){
        if(depth>64||size>16*1024*1024)throw new IOException("XISO directory is too deep or too large.");
        if(size==0)return;long start=checked((long)sector*2048);Bounds(start,size);
        if(!directories.Add(start))throw new IOException("Repeated or cyclic XISO directory.");
        var table=Read(input,start,checked((int)size));
        // A fully padded directory table represents an existing, empty folder.
        // Keep the parent's entry; no directory record exists to decode here.
        if(table.All(value=>value==0xff))return;
        var visited=new HashSet<int>();var pending=new Stack<int>();pending.Push(0);
        var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        while(pending.Count>0){
            cancel.ThrowIfCancellationRequested();int offset=pending.Pop();
            if(!visited.Add(offset)||offset<0||offset>table.Length-14)throw new IOException("Invalid XISO directory links.");
            int count=table[offset+13];if(count==0||count>table.Length-offset-14||(offset%2048)+14+count>2048)throw new IOException("Invalid XISO directory record.");
            string name=Encoding.GetEncoding(1252).GetString(table,offset+14,count);
            if(name=="."||name==".."||name.IndexOfAny(new[]{'\\','/',':','\0'})>=0||name.Any(c=>c<32)||!names.Add(name))throw new IOException("Unsafe or duplicate name inside XISO.");
            var entry=new Entry{Name=name,Relative=parent.Relative.Length==0?name:parent.Relative+"/"+name,Directory=(table[offset+12]&16)!=0,
                Offset=(long)BitConverter.ToUInt32(table,offset+4)*2048,Size=BitConverter.ToUInt32(table,offset+8)};
            if(++entries>2000000)throw new IOException("Too many XISO entries.");Bounds(entry.Offset,entry.Size);parent.Children.Add(entry);
            if(entry.Directory)ReadDirectory(input,entry,BitConverter.ToUInt32(table,offset+4),(uint)entry.Size,depth+1);
            else if(files.ContainsKey(entry.Relative))throw new IOException("Duplicate XISO path.");else files.Add(entry.Relative,entry);
            int left=BitConverter.ToUInt16(table,offset)*4,right=BitConverter.ToUInt16(table,offset+2)*4;
            if(left!=0)pending.Push(left);if(right!=0)pending.Push(right);
        }
        parent.Children.Sort((a,b)=>StringComparer.OrdinalIgnoreCase.Compare(a.Name,b.Name));
    }
    public bool Exists(string relative){return files.ContainsKey(relative.Replace('\\','/'));}
    public byte[] SmallFile(string relative){Entry entry;if(!files.TryGetValue(relative.Replace('\\','/'),out entry)||entry.Size>64*1024*1024)throw new IOException("Missing or oversized image patch target: "+relative);using(var stream=OpenContainer())return Read(stream,entry.Offset,checked((int)entry.Size));}
    public Stream Open(Entry entry){return new SliceStream(OpenContainer(),entry.Offset,entry.Size,cancel);}
    public Stream OpenImage(){return new SliceStream(OpenContainer(),0,length,cancel);}
    public string DestinationName(){
        string outer=Path.GetFileName(image);while(Candidate(outer))outer=Path.GetFileNameWithoutExtension(outer);
        outer=Regex.Replace(outer,@"\s*[\(\[].*$","").Trim();
        string chosen=!String.IsNullOrWhiteSpace(outer)&&!new[]{"game","default","image","disc","dvd"}.Contains(outer,StringComparer.OrdinalIgnoreCase)?outer:title;
        if(String.IsNullOrWhiteSpace(chosen))chosen="Xbox Game";
        var clean=new StringBuilder();foreach(char c in chosen){if(c>=32&&c<=126&&"\\/:*?\"<>|+,;=".IndexOf(c)<0)clean.Append(c);else if(c>126)clean.Append('_');}
        string result=Regex.Replace(clean.ToString(),@"\s+"," ").Trim().TrimEnd('.');if(result.Length>42)result=result.Substring(0,42).TrimEnd(' ','.');if(result.Length==0||result==".")result="Xbox Game";return result;
    }
    sealed class SliceStream:Stream {
        readonly FileStream source;readonly long start,size;readonly CancellationToken cancel;long position;
        public SliceStream(FileStream input,long offset,long length,CancellationToken token){source=input;start=offset;size=length;cancel=token;source.Position=start;}
        public override int Read(byte[] buffer,int offset,int count){cancel.ThrowIfCancellationRequested();int wanted=(int)Math.Min(count,size-position);if(wanted==0)return 0;int got=source.Read(buffer,offset,wanted);if(got==0)throw new IOException("Truncated image file entry.");position+=got;return got;}
        public override bool CanRead{get{return true;}}public override bool CanSeek{get{return false;}}public override bool CanWrite{get{return false;}}
        public override long Length{get{return size;}}public override long Position{get{return position;}set{throw new NotSupportedException();}}
        public override void Flush(){}public override long Seek(long o,SeekOrigin s){throw new NotSupportedException();}public override void SetLength(long v){throw new NotSupportedException();}public override void Write(byte[] b,int o,int c){throw new NotSupportedException();}
        protected override void Dispose(bool disposing){if(disposing)source.Dispose();base.Dispose(disposing);}
    }
}

internal sealed class XisoImport {
    internal sealed class Item {public string Name;public bool Directory;public long Size;public byte[] Data;public Func<Stream> Open;public readonly List<Item> Children=new List<Item>();}
    public readonly Item Root;public readonly GameCompatibility Game;
    public XisoImport(string path,Func<GameCompatibility,bool> approval,CancellationToken token){
        var image=new XisoReader(path,token);Game=GameCompatibility.ForImage(image,approval,token);
        Root=new Item{Name=Game!=null&&Game.FolderName!=null?Game.FolderName:image.DestinationName(),Directory=true};
        if(Game!=null&&Game.KeepImage){
            if(image.Length>uint.MaxValue)throw new IOException("This game needs image loading, but its image exceeds the single-file FATX limit. Split-image import is not supported yet.");
            Root.Children.Add(new Item{Name="game.iso",Size=image.Length,Open=image.OpenImage});Root.Children.Add(new Item{Name="default.xbe",Data=Game.ImageLauncher,Size=Game.ImageLauncher.LongLength});return;
        }
        Action<XisoReader.Entry,Item> add=null;add=(entry,parent)=>{
            token.ThrowIfCancellationRequested();string key=image.Key+"\\"+entry.Relative.Replace('/','\\');if(!entry.Directory&&Game!=null&&Game.Skip(key))return;
            var item=new Item{Name=Game==null?entry.Name:Game.Name(key),Directory=entry.Directory,Size=entry.Directory?0:entry.Size};FatxNames.Validate(item.Name);
            if(item.Directory)foreach(var child in entry.Children)add(child,item);
            else{if(item.Size>uint.MaxValue)throw new IOException("Image entry exceeds FATX file-size limit.");item.Data=Game==null?null:Game.Replacement(key);if(item.Data!=null)item.Size=item.Data.LongLength;else item.Open=()=>image.Open(entry);}
            if(parent.Children.Any(c=>c.Name.Equals(item.Name,StringComparison.OrdinalIgnoreCase)))throw new IOException("Image destination names conflict after patching.");parent.Children.Add(item);
        };
        foreach(var entry in image.Root.Children)add(entry,Root);FatxNames.Validate(Root.Name);
    }
}
}
