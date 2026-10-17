using System;

using System.Collections.Generic;

using System.Collections.Concurrent;

using System.Diagnostics;

using System.IO;

using System.IO.Compression;

using System.Linq;

using System.Reflection;

using System.Text;

using System.Threading;

using System.Threading.Tasks;

namespace RichRetro.Fatx {

internal sealed class Fatx {

    internal const long FStart=0xEE8AB0, MaxExtendedSectors=0x80000000L;

    internal static bool NeedsG(long bytes){return bytes/512-FStart>MaxExtendedSectors;} const int Batch=1024*1024;

    static readonly long[] Starts={0x400,0x177400,0x2EE400,0x465400,0x55F400},Sizes={0x177000,0x177000,0x177000,0xFA000,0x9896B0};

    static readonly string[] Names={"X","Y","Z","C","E"};

    sealed class Node {public string Name,Path,Source;public bool Dir;public byte[] Data;public XisoImage Image;public Func<Stream> OpenSource;public long Size,SourceSize;public DateTime Stamp;public int First,Clusters;public List<Node> Children=new List<Node>();}

    sealed class Volume {public string Name;public long Start,Sectors,FatBytes,DataStart,Count;public int Spc,Width;public List<Node> Nodes=new List<Node>();public Node Root;}

    sealed class Block {public long Sector;public byte[] Bytes;public string Message;public long Files;public bool Verify;}

    readonly List<Volume> volumes=new List<Volume>();readonly long sectors;long done,total,filesDone,fileTotal;readonly InstallPlan plan;

    readonly Stopwatch watch=new Stopwatch();readonly Action<string,long,long> progress;readonly CancellationToken cancel;

    readonly Func<GameCompatibility,bool> patchApproval;

    readonly List<string> skippedC=new List<string>();readonly List<GameCompatibility> gameFixes=new List<GameCompatibility>();

    public string CompletionSummary {get{

        var elapsed=watch.Elapsed;

        return "Total time: "+String.Format("{0:00}:{1:00}:{2:00}",(long)elapsed.TotalHours,elapsed.Minutes,elapsed.Seconds)+

            " | "+filesDone.ToString("N0")+" files | "+(volumes.Sum(v=>v.Nodes.Sum(n=>n.Size))/1000000000.0).ToString("0.00")+" GB copied";

    }}

    public string Summary {get{return plan.Summary+"\n"+String.Join("; ",volumes.Where(v=>v.Name=="F"||v.Name=="G").Select(v=>v.Name+": "+(v.Count*v.Spc*512/1000000000.0).ToString("0.00")+" GB usable, "+(v.Nodes.Sum(n=>(long)n.Clusters)/ (double)v.Count*100).ToString("0.0")+"% allocated"))+"\n"+fileTotal.ToString("N0")+" files, "+(volumes.Sum(v=>v.Nodes.Sum(n=>n.Size))/1073741824.0).ToString("0.00")+" GiB to install"+(gameFixes.Count==0?"":"\nAutomatic game fixes: "+String.Join("; ",gameFixes.Select(x=>x.Description)))+(skippedC.Count==0?"":"\n"+skippedC.Count+" conflicting custom C item(s) skipped; RRXD kept.");}}

    public Fatx(long size,InstallPlan options,Action<string,long,long> p,CancellationToken c,Func<GameCompatibility,bool> approval=null){

        plan=options;progress=p;cancel=c;patchApproval=approval;if(size%512!=0)throw new IOException("Capacity is not a whole number of 512-byte sectors.");sectors=size/512;

        if(sectors<=FStart)throw new IOException("Drive is too small for the Xbox layout.");

        if(sectors>uint.MaxValue)throw new IOException("This layout supports drives below 2 TiB (including standard 2 TB drives), using 512-byte sectors.");

        bool split=NeedsG(size);

        if(split&&!plan.SplitFG)throw new IOException("This drive needs F/G allocation. Choose automatic or manual placement before formatting.");

        for(int i=0;i<5;i++)volumes.Add(Prepare(Names[i],Starts[i],Sizes[i]));

        long fLength=split?MaxExtendedSectors:sectors-FStart;

        var f=Prepare("F",FStart,fLength,split&&plan.AutoSplit);

        volumes.Add(f);

        if(split){var g=Prepare("G",FStart+fLength,sectors-FStart-fLength);

            if(plan.AutoSplit)Distribute(f,g);

            volumes.Add(g);

        }

        total=67*512+volumes.Sum(v=>4096+v.FatBytes+v.Nodes.Sum(n=>(long)n.Clusters*v.Spc*512));fileTotal=volumes.Sum(v=>v.Nodes.LongCount(n=>!n.Dir));

    }

    static void Put(byte[] b,int off,uint value){Array.Copy(BitConverter.GetBytes(value),0,b,off,4);}

    static void Valid(string name){FatxNames.Validate(name);}

    static Node Add(Volume v,Dictionary<string,Node> nodes,string path,bool directory){

        var parts=path.Replace('\\','/').TrimEnd('/').Split('/');string current="";Node parent=v.Root;

        for(int i=0;i<parts.Length;i++){Valid(parts[i]);current=current.Length==0?parts[i]:current+"/"+parts[i];bool dir=i<parts.Length-1||directory;Node n;

            if(nodes.TryGetValue(current,out n)){if(n.Dir!=dir||!dir)throw new IOException("Files conflict at "+v.Name+":\\"+current+". RRXD files cannot be replaced by custom files.");}

            else{n=new Node{Name=parts[i],Path=current,Dir=dir};nodes.Add(current,n);v.Nodes.Add(n);parent.Children.Add(n);}parent=n;

        }return parent;

    }

    static bool InsideLooseGame(string directory,string root){
        for(var current=new DirectoryInfo(directory);current!=null;current=current.Parent){if(File.Exists(Path.Combine(current.FullName,"default.xbe")))return true;if(current.FullName.Equals(root,StringComparison.OrdinalIgnoreCase))break;}return false;
    }
    void AddImage(Volume v,Dictionary<string,Node> nodes,XisoImport image,string prefix){

        Action<XisoImport.Item,string> add=null;add=(item,parent)=>{cancel.ThrowIfCancellationRequested();string path=parent.Length==0?item.Name:parent+"/"+item.Name;var n=Add(v,nodes,path,item.Directory);n.Size=item.Size;n.Data=item.Data;n.OpenSource=item.Open;if(item.Directory)foreach(var child in item.Children)add(child,path);};

        add(image.Root,prefix);if(image.Game!=null)gameFixes.Add(image.Game);

    }

    Volume Prepare(string name,long start,long length,bool deferAllocation=false){

        cancel.ThrowIfCancellationRequested();progress("Checking "+name+" files and space before erasing",0,0);

        var v=new Volume{Name=name,Start=start,Sectors=length,Root=new Node{Name="",Path="",Dir=true}};v.Nodes.Add(v.Root);var nodes=new Dictionary<string,Node>(StringComparer.OrdinalIgnoreCase);nodes.Add("",v.Root);

        string folder=name=="C"?plan.CFolder:name=="E"?plan.EFolder:name=="F"?plan.FFolder:name=="G"?plan.GFolder:null;var images=name=="F"?plan.FImages:name=="G"?plan.GImages:new string[0];bool bundled=name=="C"?plan.Rrxd:(name=="E"||name=="F"||name=="G")&&folder==null&&images.Length==0;

        if(bundled)using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream((name=="G"?"F":name)+".zip")){

            if(stream==null)throw new IOException("Missing bundled payload: "+name);

            using(var zip=new ZipArchive(stream,ZipArchiveMode.Read))foreach(var e in zip.Entries){cancel.ThrowIfCancellationRequested();string path=e.FullName.Replace('\\','/').TrimEnd('/');if(path.Length==0)continue;bool dir=e.FullName.EndsWith("/")||e.FullName.EndsWith("\\");var n=Add(v,nodes,path,dir);

                if(!dir)using(var input=e.Open())using(var memory=new MemoryStream()){input.CopyTo(memory);n.Data=memory.ToArray();n.Size=n.Data.LongLength;if(n.Size!=e.Length)throw new IOException("Truncated bundled file.");}

            }

        }

        var bundledPaths=new HashSet<string>(nodes.Keys,StringComparer.OrdinalIgnoreCase);

        if(folder!=null){var source=new DirectoryInfo(folder);if(!source.Exists)throw new IOException("Source folder missing: "+folder);var pending=new Stack<Tuple<DirectoryInfo,string>>();string rootPrefix=name=="F"?plan.FPrefix:name=="G"?plan.GPrefix:"";if(rootPrefix.Length>0)Add(v,nodes,rootPrefix,true);pending.Push(Tuple.Create(source,rootPrefix));int scanned=0;

            while(pending.Count>0){var item=pending.Pop();

                if((name=="F"||name=="G")&&!gameFixes.Any(x=>x.Root.Equals(item.Item1.FullName,StringComparison.OrdinalIgnoreCase))){var found=GameCompatibility.ForFolder(item.Item1.FullName,patchApproval,cancel);if(found!=null)gameFixes.Add(found);}

                var game=gameFixes.Where(x=>item.Item1.FullName.Equals(x.Root,StringComparison.OrdinalIgnoreCase)||item.Item1.FullName.StartsWith(x.Root+"\\",StringComparison.OrdinalIgnoreCase)).OrderByDescending(x=>x.Root.Length).FirstOrDefault();

                if(game!=null&&game.Image!=null&&item.Item1.FullName.Equals(game.Root,StringComparison.OrdinalIgnoreCase)){

                    string prefix=item.Item2.Length==0?"":item.Item2+"/";

                    var imageNode=Add(v,nodes,prefix+"game.iso",false);imageNode.Image=game.Image;imageNode.Size=game.Image.Length;

                    var launcherNode=Add(v,nodes,prefix+"default.xbe",false);launcherNode.Data=game.ImageLauncher;launcherNode.Size=launcherNode.Data.LongLength;

                    continue;

                }

                foreach(var f in item.Item1.EnumerateFileSystemInfos()){

                if(!Directory.Exists(f.FullName)&&XisoReader.Candidate(f.FullName)&&(name=="F"||name=="G")&&game==null&&!InsideLooseGame(item.Item1.FullName,folder)){AddImage(v,nodes,new XisoImport(f.FullName,patchApproval,cancel),item.Item2.Length==0?"Games":item.Item2);continue;}

                cancel.ThrowIfCancellationRequested();if((f.Attributes&FileAttributes.ReparsePoint)!=0)throw new IOException("Links/junctions are not supported: "+f.FullName);bool dir=(f.Attributes&FileAttributes.Directory)!=0;if(!dir&&game!=null&&game.Skip(f.FullName))continue;

                var childGame=(name=="F"||name=="G")&&dir?GameCompatibility.ForFolder(f.FullName,patchApproval,cancel):null;if(childGame!=null&&!gameFixes.Any(x=>x.Root.Equals(childGame.Root,StringComparison.OrdinalIgnoreCase)))gameFixes.Add(childGame);

                string destinationName=childGame!=null&&childGame.FolderName!=null?childGame.FolderName:game==null?f.Name:game.Name(f.FullName);

                string parent=item.Item2;if(parent.Length==0&&dir&&((name=="F"&&plan.FLooseGames)||(name=="G"&&plan.GLooseGames))&&File.Exists(Path.Combine(f.FullName,"default.xbe")))parent="Games";string path=parent.Length==0?destinationName:parent+"/"+destinationName;

                Node existing;

                if(name=="C" && plan.Rrxd && bundledPaths.Contains(path) && nodes.TryGetValue(path,out existing) && (!existing.Dir || !dir)){

                    if(!plan.SkipCConflicts)throw new CConflictException(path);

                    skippedC.Add(path+(dir?"/ (folder and contents)":""));continue;

                }

                var n=Add(v,nodes,path,dir);

                if(dir)pending.Push(Tuple.Create((DirectoryInfo)f,path));else{var file=(FileInfo)f;n.Source=file.FullName;n.Stamp=file.LastWriteTimeUtc;n.SourceSize=n.Size=file.Length;if(game!=null)n.Data=game.Replacement(f.FullName);if(n.Data!=null)n.Size=n.Data.LongLength;if(n.Size>uint.MaxValue)throw new IOException("FATX files must be smaller than 4 GiB: "+f.FullName);}if(++scanned%250==0)progress("Checking "+name+": "+scanned.ToString("N0")+" items",0,0);

            }}

        }

        foreach(string image in images){cancel.ThrowIfCancellationRequested();AddImage(v,nodes,new XisoImport(image,patchApproval,cancel),"Games");}
        v.Spc=v.Sectors>0x20000000L?128:v.Sectors>0x10000000L?64:32;long entries=v.Sectors/v.Spc+1;v.Width=entries>=0xFFF0?4:2;v.FatBytes=((entries*v.Width+4095)/4096)*4096;v.DataStart=8+v.FatBytes/512;v.Count=(v.Sectors*512-4096-v.FatBytes)/(v.Spc*512);

        if(!deferAllocation)Allocate(v);return v;

    }

    static void Allocate(Volume v){

        int next=1;foreach(var n in v.Nodes.Where(n=>n.Dir).Concat(v.Nodes.Where(n=>!n.Dir))){long bytes=n.Dir?(n.Children.Count+1L)*64:n.Size;n.Clusters=checked((int)((bytes+v.Spc*512-1)/(v.Spc*512)));n.First=n.Clusters==0?0:next;next=checked(next+n.Clusters);}

        if(next-1>v.Count)throw new IOException(v.Name+" files need "+((next-1L)*v.Spc*512/1073741824.0).ToString("0.00")+" GiB, but only "+(v.Count*v.Spc*512/1073741824.0).ToString("0.00")+" GiB is available.");

    }

    static long Cost(Node n,Volume v){return (n.Dir?((n.Children.Count+1L)*64+v.Spc*512-1)/(v.Spc*512):(n.Size+v.Spc*512-1)/(v.Spc*512))+n.Children.Sum(x=>Cost(x,v));}

    static void Index(Volume v){v.Nodes.Clear();var stack=new Stack<Node>();stack.Push(v.Root);while(stack.Count>0){var n=stack.Pop();v.Nodes.Add(n);foreach(var child in n.Children.AsEnumerable().Reverse())stack.Push(child);}}

    void Distribute(Volume f,Volume g){

        // Games/Apps are shared containers; each individual game/app stays whole.

        bool spill=false;long used=((f.Root.Children.Count+1L)*64+f.Spc*512-1)/(f.Spc*512);

        used+=f.Root.Children.Where(n=>n.Dir&&(n.Name.Equals("Games",StringComparison.OrdinalIgnoreCase)||n.Name.Equals("Apps",StringComparison.OrdinalIgnoreCase))).Sum(n=>((n.Children.Count+1L)*64+f.Spc*512-1)/(f.Spc*512));

        foreach(var root in f.Root.Children.OrderBy(n=>n.Name,StringComparer.OrdinalIgnoreCase).ToArray()){

            cancel.ThrowIfCancellationRequested();

            bool container=root.Dir&&(root.Name.Equals("Games",StringComparison.OrdinalIgnoreCase)||root.Name.Equals("Apps",StringComparison.OrdinalIgnoreCase));

            Node destination=g.Root;

            if(container){destination=g.Root.Children.FirstOrDefault(n=>n.Name.Equals(root.Name,StringComparison.OrdinalIgnoreCase));

                if(destination==null){destination=new Node{Name=root.Name,Path=root.Path,Dir=true};g.Root.Children.Add(destination);}

            }

            foreach(var item in (container?root.Children.ToArray():new[]{root}).OrderBy(n=>n.Name,StringComparer.OrdinalIgnoreCase)){

                cancel.ThrowIfCancellationRequested();long needed=Cost(item,f);

                if(!spill&&used+needed<=f.Count){used+=needed;continue;}

                spill=true;(container?root.Children:f.Root.Children).Remove(item);destination.Children.Add(item);

            }

        }

        Index(f);Index(g);Allocate(f);Allocate(g);

    }



    void Update(string message){double seconds=watch.Elapsed.TotalSeconds,speed=seconds>0?done/seconds:0;string eta=speed>0?TimeSpan.FromSeconds((total-done)/speed).ToString(@"hh\:mm\:ss"):"estimating";progress(message+"\n"+filesDone.ToString("N0")+" / "+fileTotal.ToString("N0")+" files  |  "+(done/1073741824.0).ToString("0.00")+" / "+(total/1073741824.0).ToString("0.00")+" GiB  |  "+(speed*8/1000000).ToString("0.0")+" Mbps  |  ETA "+eta,done,total);}

    void Write(RawDisk disk,long sector,byte[] bytes,string message){cancel.ThrowIfCancellationRequested();Update(message);disk.Write(sector,bytes,cancel);done+=bytes.Length;Update(message);}

    void CopyVolume(RawDisk disk,Volume v){

        using(var linked=CancellationTokenSource.CreateLinkedTokenSource(cancel))using(var queue=new BlockingCollection<Block>(2)){

            Exception failure=null;var worker=Task.Factory.StartNew(()=>{try{foreach(var block in queue.GetConsumingEnumerable(linked.Token)){disk.Write(block.Sector,block.Bytes,linked.Token,block.Verify);done+=block.Bytes.Length;filesDone=block.Files;Update(block.Message);}}catch(Exception e){failure=e;linked.Cancel();}},CancellationToken.None,TaskCreationOptions.LongRunning,TaskScheduler.Default);

            byte[] buffer=new byte[Batch];int used=0;long nextSector=v.Start+v.DataStart,completed=filesDone;string label="Installing "+v.Name;bool verifyBlock=true;bool copyingFiles=false;

            Action flush=()=>{if(used==0)return;byte[] data=buffer;if(used!=Batch){data=new byte[used];Array.Copy(buffer,data,used);}queue.Add(new Block{Sector=nextSector,Bytes=data,Message=label,Files=completed,Verify=verifyBlock},linked.Token);nextSector+=used/512;buffer=new byte[Batch];used=0;};

            Action<byte[],int,int> append=(data,offset,length)=>{while(length>0){linked.Token.ThrowIfCancellationRequested();int n=Math.Min(length,Batch-used);Array.Copy(data,offset,buffer,used,n);used+=n;offset+=n;length-=n;if(used==Batch)flush();}};

            var readBuffer=new byte[Batch];

            try{

                foreach(var n in v.Nodes.Where(n=>n.Dir).Concat(v.Nodes.Where(n=>!n.Dir))){linked.Token.ThrowIfCancellationRequested();label="Installing "+v.Name+":\\"+n.Path;

                    if(n.Dir){var data=Enumerable.Repeat((byte)255,n.Clusters*v.Spc*512).ToArray();int pos=0;foreach(var child in n.Children){Array.Clear(data,pos,64);var encodedName=FatxNames.Encode(child.Name);data[pos]=(byte)encodedName.Length;data[pos+1]=(byte)(child.Dir?16:0);Array.Copy(encodedName,0,data,pos+2,encodedName.Length);Put(data,pos+44,(uint)child.First);Put(data,pos+48,child.Dir?0:(uint)child.Size);pos+=64;}append(data,0,data.Length);}

                    else{

                        if(!copyingFiles){flush();copyingFiles=true;verifyBlock=plan.FullVerify||(v.Name!="F"&&v.Name!="G");}

                        if(n.Source!=null){var current=new FileInfo(n.Source);if((current.Attributes&FileAttributes.ReparsePoint)!=0||current.Length!=n.SourceSize||current.LastWriteTimeUtc!=n.Stamp)throw new IOException("Source changed: "+n.Path);}

                        using(var input=n.OpenSource!=null?n.OpenSource():n.Image!=null?n.Image.Open():n.Data!=null?(Stream)new MemoryStream(n.Data,false):new FileStream(n.Source,FileMode.Open,FileAccess.Read,FileShare.Read,4096,FileOptions.SequentialScan)){

                            if(input.Length!=n.Size || (n.Source!=null && File.GetLastWriteTimeUtc(n.Source)!=n.Stamp))throw new IOException("Source changed: "+n.Path);var read=readBuffer;long remaining=n.Size;while(remaining>0){int want=(int)Math.Min(Batch,remaining),got=0;while(got<want){int r=input.Read(read,got,want-got);if(r==0)throw new IOException("Short source read: "+n.Path);got+=r;}append(read,0,got);remaining-=got;}}

                        int padding=(int)((long)n.Clusters*v.Spc*512-n.Size);if(padding>0)append(new byte[padding],0,padding);completed++;

                    }

                }flush();

            }finally{queue.CompleteAdding();worker.GetAwaiter().GetResult();if(failure!=null)throw failure;}

            filesDone=completed;Update("Finished "+v.Name);

        }

    }

    public void Run(Target target){

        Storage.ProtectSources(target,plan);watch.Start();using(var disk=new RawDisk(target)){

            Write(disk,0,new byte[512],"Removing old PC partition metadata");Write(disk,1,new byte[33*512],"Removing primary PC partition metadata");Write(disk,sectors-33,new byte[33*512],"Removing backup PC partition metadata");

            foreach(var v in volumes){var header=new byte[4096];Array.Copy(Encoding.ASCII.GetBytes("FATX"),header,4);Put(header,4,unchecked((uint)Environment.TickCount));Put(header,8,(uint)v.Spc);Put(header,12,1);Write(disk,v.Start,header,"Formatting "+v.Name);

                var allocated=v.Nodes.Where(n=>n.Clusters>0).OrderBy(n=>n.First).ToArray();int range=0;

                for(long off=0;off<v.FatBytes;off+=Batch){var fat=new byte[(int)Math.Min(Batch,v.FatBytes-off)];for(int i=0;i<fat.Length;i+=v.Width){long id=(off+i)/v.Width;while(range<allocated.Length&&id>=allocated[range].First+(long)allocated[range].Clusters)range++;uint value=0;

                    if(id==0)value=v.Width==2?0xFFF8u:0xFFFFFFF8u;else if(range<allocated.Length&&id>=allocated[range].First)value=id==allocated[range].First+(long)allocated[range].Clusters-1?(v.Width==2?0xFFFFu:uint.MaxValue):(uint)(id+1);

                    if(v.Width==4)Put(fat,i,value);else{fat[i]=(byte)value;fat[i+1]=(byte)(value>>8);}}Write(disk,v.Start+8+off/512,fat,"Formatting "+v.Name+" allocation table");}CopyVolume(disk,v);

            }

            disk.Verify(progress,cancel);Storage.Recheck(target);var table=new byte[512];Array.Copy(Encoding.ASCII.GetBytes("****PARTINFO****"),table,16);var titles=new[]{"XBOX SHELL","XBOX DATA","XBOX GAME SWAP 1","XBOX GAME SWAP 2","XBOX GAME SWAP 3","XBOX F","XBOX G"};int[] order={4,3,0,1,2,5,6};

            for(int i=0;i<14;i++){int off=48+i*32;for(int j=0;j<16;j++)table[off+j]=32;if(i<titles.Length)Array.Copy(Encoding.ASCII.GetBytes(titles[i]),0,table,off,titles[i].Length);if(i<order.Length&&order[i]<volumes.Count){var v=volumes[order[i]];Put(table,off+16,0x80000000);Put(table,off+20,(uint)v.Start);Put(table,off+24,(uint)v.Sectors);}}

            cancel.ThrowIfCancellationRequested();disk.BeginFinalTableVerification();disk.Write(0,table,cancel);disk.Verify((m,a,b)=>progress("Verifying final partition table",a,b),cancel);

            watch.Stop();

        }

    }

}

}

