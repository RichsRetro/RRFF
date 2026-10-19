using System;

using System.Collections.Generic;

using System.IO;

using System.Linq;

using System.Text;

using System.Reflection;

using System.Threading;

namespace RichRetro.Fatx {

// A planned XDVDFS image streamed directly into the existing FATX writer.

// Source files are read unchanged; no child process or temporary image is used.

internal sealed class XisoImage {

    sealed class Entry {public string Path,Name;public byte[] Encoded;public bool Directory;public long Size;public DateTime Stamp;public uint Sector;public int RecordOffset;public byte[] Table;public List<Entry> Children=new List<Entry>();}

    sealed class Piece {public long Offset,Size;public byte[] Data;public Entry File;}

    readonly List<Entry> directories=new List<Entry>(),files=new List<Entry>();

    readonly List<Piece> pieces=new List<Piece>();readonly CancellationToken cancel;

    public long Length {get;private set;}

    static long Align(long n,int boundary){return checked((n+boundary-1)/boundary*boundary);}

    static byte[] NameBytes(string name){if(name.Length==0||name.Length>255||name.Any(c=>c<32||c>126||c=='/'||c=='\\'))throw new IOException(L.T("Unsupported XISO filename: ")+name);return Encoding.ASCII.GetBytes(name);}

    Entry Scan(string path,int depth){

        cancel.ThrowIfCancellationRequested();if(depth>64)throw new IOException(L.T("Game folder is too deep."));

        var info=new FileInfo(path);var attr=File.GetAttributes(path);if((attr&FileAttributes.ReparsePoint)!=0)throw new IOException(L.T("Links/junctions are not supported in game images."));

        var e=new Entry{Path=path,Name=System.IO.Path.GetFileName(path),Directory=(attr&FileAttributes.Directory)!=0};

        if(e.Directory){directories.Add(e);foreach(string child in Directory.EnumerateFileSystemEntries(path).OrderBy(p=>System.IO.Path.GetFileName(p),StringComparer.OrdinalIgnoreCase))e.Children.Add(Scan(child,depth+1));

            var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);int offset=0;

            foreach(var child in e.Children){if(!names.Add(child.Name))throw new IOException(L.T("Case-insensitive image-name collision: ")+child.Name);child.Encoded=NameBytes(child.Name);int bytes=(int)Align(14+child.Encoded.Length,4);if(offset%2048+bytes>2048)offset=(int)Align(offset,2048);child.RecordOffset=offset;offset+=bytes;}

            if(offset>0x3fffc)throw new IOException(L.T("An XISO directory exceeds its entry-offset limit."));e.Table=new byte[offset];e.Size=offset;

        }else{e.Size=info.Length;e.Stamp=info.LastWriteTimeUtc;if(e.Size>uint.MaxValue)throw new IOException(L.T("Game file exceeds the XDVDFS size limit."));files.Add(e);}

        return e;

    }

    static void Put(byte[] data,int offset,uint n){Array.Copy(BitConverter.GetBytes(n),0,data,offset,4);}

    static void PutLink(byte[] data,int offset,int n){Array.Copy(BitConverter.GetBytes(checked((ushort)(n/4))),0,data,offset,2);}

    // Root directory entries start at offset zero. Allocate each balanced tree

    // in preorder so the root is first, while links retain sorted lookup order.

    static List<Entry> Preorder(List<Entry> sorted){var result=new List<Entry>();Action<int,int> visit=null;visit=(lo,hi)=>{if(lo>=hi)return;int mid=(lo+hi)/2;result.Add(sorted[mid]);visit(lo,mid);visit(mid+1,hi);};visit(0,sorted.Count);return result;}

    void LayoutTree(Entry dir){

        if(dir.Children.Count==0)return;int offset=0;

        foreach(var e in Preorder(dir.Children)){int bytes=(int)Align(14+e.Encoded.Length,4);if(offset%2048+bytes>2048)offset=(int)Align(offset,2048);e.RecordOffset=offset;offset+=bytes;}

        if(offset>0x3fffc)throw new IOException(L.T("An XISO directory exceeds its entry-offset limit."));dir.Table=new byte[offset];dir.Size=offset;

        Action<int,int> write=null;write=(lo,hi)=>{if(lo>=hi)return;int mid=(lo+hi)/2;var e=dir.Children[mid];int p=e.RecordOffset;

            if(lo<mid)PutLink(dir.Table,p,dir.Children[(lo+mid)/2].RecordOffset);

            if(mid+1<hi)PutLink(dir.Table,p+2,dir.Children[(mid+1+hi)/2].RecordOffset);

            Put(dir.Table,p+4,e.Sector);Put(dir.Table,p+8,checked((uint)e.Size));dir.Table[p+12]=(byte)(e.Directory?16:32);dir.Table[p+13]=(byte)e.Encoded.Length;Array.Copy(e.Encoded,0,dir.Table,p+14,e.Encoded.Length);write(lo,mid);write(mid+1,hi);

        };write(0,dir.Children.Count);

    }

    public XisoImage(string root,CancellationToken token){cancel=token;var top=Scan(root,0);

        // Preorder changes padding positions; compute tables before allocation.

        foreach(var d in directories)LayoutTree(d);

        long position=33L*2048;

        foreach(var d in directories){if(d.Size==0){d.Sector=0;continue;}d.Sector=checked((uint)(position/2048));position+=Align(d.Size,2048);}

        foreach(var f in files){f.Sector=checked((uint)(position/2048));position+=Align(f.Size,2048);}

        Length=Align(position,2048);if(Length>uint.MaxValue)throw new IOException(L.T("This release needs a split XISO; automatic conversion currently supports single-file images up to 4 GiB."));

        var header=new byte[2048];var magic=Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA");Array.Copy(magic,header,20);Put(header,20,top.Sector);Put(header,24,checked((uint)top.Size));Array.Copy(BitConverter.GetBytes(DateTime.UtcNow.ToFileTimeUtc()),0,header,28,8);Array.Copy(magic,0,header,2028,20);pieces.Add(new Piece{Offset=65536,Size=2048,Data=header});

        foreach(var d in directories){LayoutTree(d);if(d.Size>0)pieces.Add(new Piece{Offset=d.Sector*2048L,Size=d.Size,Data=d.Table});}

        foreach(var f in files)if(f.Size>0)pieces.Add(new Piece{Offset=f.Sector*2048L,Size=f.Size,File=f});

    }

    public Stream Open(){return new ImageStream(this);}

    public static byte[] Launcher(uint titleId,string title){

        byte[] data;using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("xiso-launcher.xbe"))using(var memory=new MemoryStream()){if(stream==null)throw new IOException(L.T("Missing XISO launcher."));stream.CopyTo(memory);data=memory.ToArray();}

        int cert=checked((int)(BitConverter.ToUInt32(data,0x118)-BitConverter.ToUInt32(data,0x104)));Put(data,cert+8,titleId);Array.Clear(data,cert+12,80);byte[] name=Encoding.Unicode.GetBytes(title.Substring(0,Math.Min(39,title.Length)));Array.Copy(name,0,data,cert+12,name.Length);return data;

    }

    sealed class ImageStream:Stream {

        readonly XisoImage image;long position;int index;FileStream current;

        public ImageStream(XisoImage value){image=value;}

        void CloseFile(){if(current!=null){var e=image.pieces[index].File;current.Dispose();current=null;if(File.GetLastWriteTimeUtc(e.Path)!=e.Stamp)throw new IOException(L.T("Game source changed: ")+e.Name);}}

        public override int Read(byte[] buffer,int offset,int count){image.cancel.ThrowIfCancellationRequested();int wanted=(int)Math.Min(count,image.Length-position),remaining=wanted;

            while(remaining>0){image.cancel.ThrowIfCancellationRequested();if(index<image.pieces.Count&&position>=image.pieces[index].Offset+image.pieces[index].Size){CloseFile();index++;continue;}

                var piece=index<image.pieces.Count?image.pieces[index]:null;

                if(piece==null||position<piece.Offset){int zeros=(int)Math.Min(remaining,(piece==null?image.Length:piece.Offset)-position);Array.Clear(buffer,offset,zeros);offset+=zeros;remaining-=zeros;position+=zeros;continue;}

                int n=(int)Math.Min(remaining,piece.Offset+piece.Size-position);

                if(piece.Data!=null)Array.Copy(piece.Data,position-piece.Offset,buffer,offset,n);

                else{if(current==null){var e=piece.File;var info=new FileInfo(e.Path);if((info.Attributes&FileAttributes.ReparsePoint)!=0||info.Length!=e.Size||info.LastWriteTimeUtc!=e.Stamp)throw new IOException(L.T("Game source changed: ")+e.Name);current=new FileStream(e.Path,FileMode.Open,FileAccess.Read,FileShare.Read,65536,FileOptions.SequentialScan);}

                    int got=0;while(got<n){int read=current.Read(buffer,offset+got,n-got);if(read==0)throw new IOException(L.T("Short game source read."));got+=read;}}

                offset+=n;remaining-=n;position+=n;

            }return wanted;

        }

        protected override void Dispose(bool disposing){if(disposing)CloseFile();base.Dispose(disposing);}

        public override bool CanRead {get{return true;}}public override bool CanSeek {get{return false;}}public override bool CanWrite {get{return false;}}

        public override long Length {get{return image.Length;}}public override long Position {get{return position;}set{throw new NotSupportedException();}}

        public override void Flush(){}public override long Seek(long o,SeekOrigin s){throw new NotSupportedException();}public override void SetLength(long v){throw new NotSupportedException();}public override void Write(byte[] b,int o,int n){throw new NotSupportedException();}

    }

}

}

