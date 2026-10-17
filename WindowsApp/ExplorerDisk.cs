using System;

using System.Collections.Generic;

using System.Collections.Concurrent;

using System.IO;

using System.Linq;

using System.Text;

using System.Threading;

using System.Threading.Tasks;

namespace RichRetro.Fatx {

internal sealed class XboxEntry {

    public string Name;public bool Directory;public uint First,Size;public long Slot=-1;

    public override string ToString(){return Name;}

}

internal sealed class XboxPartition {

    public string Name;public long Start,Sectors,FatBytes,DataStart,Count;public int ClusterBytes,Width;public uint Root;

    public override string ToString(){return Name+":\\";}

}

internal sealed class ExplorerDisk:IDisposable {

    readonly Target target;readonly RawDisk disk;readonly CancellationToken token;

    public readonly List<XboxPartition> Partitions=new List<XboxPartition>();

    readonly List<string> headerIssues=new List<string>();bool tablePresent;

    readonly List<string> importFixes=new List<string>();

    public string LastGameFixSummary {get{return importFixes.Count==0?"":" | Game fixes: "+String.Join("; ",importFixes.Distinct());}}

    XboxPartition volume;byte[] fat;bool ownershipChecked,faulted;uint nextFree=1;

    public ExplorerDisk(Target selected,CancellationToken cancel,bool readOnly=false){

        target=selected;token=cancel;if(target.Sector!=512)throw new IOException("FATX browsing requires 512-byte logical sectors.");

        disk=new RawDisk(target,readOnly);

        try{

            var table=disk.Read(0,512,token);

            tablePresent=Encoding.ASCII.GetString(table,0,16)=="****PARTINFO****";

            if(tablePresent){int[] indexes={1,0,2,3,4,5,6};string[] names={"C","E","X","Y","Z","F","G"};

                for(int i=0;i<indexes.Length;i++){int off=48+indexes[i]*32;uint flags=U32(table,off+16),start=U32(table,off+20),size=U32(table,off+24);if((flags&0x80000000)!=0&&size!=0)AddPartition(names[i],start,size);}

            }else{

                // Standard retail partitions have fixed bounds. Do not guess extended F/G sizes.

                AddPartition("C",0x465400,0xFA000);AddPartition("E",0x55F400,0x9896B0);

                AddPartition("X",0x400,0x177000);AddPartition("Y",0x177400,0x177000);AddPartition("Z",0x2EE400,0x177000);

            }

            if(Partitions.Count==0)throw new IOException("No valid FATX partitions found. This drive was not changed.");

            var sorted=Partitions.OrderBy(p=>p.Start).ToArray();for(int i=1;i<sorted.Length;i++)if(sorted[i-1].Start+sorted[i-1].Sectors>sorted[i].Start)throw new IOException("Overlapping FATX partitions. Editing blocked.");

        }catch{disk.Dispose();throw;}

    }

    static uint U32(byte[] b,int off){return BitConverter.ToUInt32(b,off);}

    void AddPartition(string name,long start,long sectors){

        if(start<1||sectors<16||start>target.Size/512-sectors)throw new IOException("Partition outside selected-drive bounds.");

        var header=disk.Read(start,4096,token);if(Encoding.ASCII.GetString(header,0,4)!="FATX"){if(tablePresent)headerIssues.Add(name+": enabled partition has no valid FATX header");return;}

        uint spc=U32(header,8);if(spc<1||spc>128||(spc&(spc-1))!=0)throw new IOException("Unsupported FATX cluster size.");

        long entries=sectors/spc+1;int width=entries>=0xFFF0?4:2;long fatBytes=((entries*width+4095)/4096)*4096;

        long count=(sectors*512-4096-fatBytes)/(spc*512);uint root=U32(header,12);

        if(count<1||count>0x0FFFFFF0||root<1||root>count||fatBytes>int.MaxValue)throw new IOException("Invalid FATX geometry.");

        Partitions.Add(new XboxPartition{Name=name,Start=start,Sectors=sectors,FatBytes=fatBytes,DataStart=start+8+fatBytes/512,Count=count,ClusterBytes=(int)spc*512,Width=width,Root=root});

    }

    public void Select(XboxPartition selected){

        if(!Partitions.Contains(selected))throw new IOException("Unknown partition.");

        volume=selected;fat=disk.Read(volume.Start+8,checked((int)volume.FatBytes),token);ownershipChecked=false;nextFree=1;

    }

    public XboxEntry Root {get{return new XboxEntry{Name=volume.Name+":",Directory=true,First=volume.Root};}}

    uint GetFat(uint id){if(id<1||id>volume.Count)throw new IOException("Cluster outside partition.");int off=checked((int)id*volume.Width);return volume.Width==4?U32(fat,off):BitConverter.ToUInt16(fat,off);}

    uint End {get{return volume.Width==4?uint.MaxValue:0xFFFFu;}}

    void SetFat(uint id,uint value){int off=checked((int)id*volume.Width);var b=BitConverter.GetBytes(value);Array.Copy(b,0,fat,off,volume.Width);}

    long Sector(uint cluster){if(cluster<1||cluster>volume.Count)throw new IOException("Cluster outside partition.");return volume.DataStart+(cluster-1L)*volume.ClusterBytes/512;}

    List<uint> Chain(XboxEntry item){

        var result=new List<uint>();if(item.First==0){if(item.Directory||item.Size!=0)throw new IOException("Missing cluster chain: "+item.Name);return result;}

        var visited=new HashSet<uint>();uint id=item.First;

        while(true){token.ThrowIfCancellationRequested();if(id<1||id>volume.Count||!visited.Add(id))throw new IOException("Invalid/cyclic cluster chain: "+item.Name);result.Add(id);uint next=GetFat(id);if(next>= (volume.Width==4?0xFFFFFFF8u:0xFFF8u))break;if(next==0)throw new IOException("Unallocated cluster in "+item.Name);id=next;}

        if(!item.Directory&&(long)result.Count*volume.ClusterBytes<item.Size)throw new IOException("Short cluster chain: "+item.Name);return result;

    }

    public List<XboxEntry> List(XboxEntry folder){

        if(!folder.Directory)throw new IOException("Not a folder.");var entries=new List<XboxEntry>();var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach(uint id in Chain(folder)){var bytes=disk.Read(Sector(id),volume.ClusterBytes,token);

            for(int pos=0;pos<bytes.Length;pos+=64){int len=bytes[pos];if(len==0||len==255)return entries;if(len==0xE5)continue;if(len>42)throw new IOException("Invalid FATX directory entry.");

                string name=FatxNames.Decode(bytes,pos+2,len);if(name=="."||name==".."||name.IndexOfAny(new[]{'\\','/',':','\0'})>=0||!names.Add(name))throw new IOException("Unsafe or duplicate directory entry.");

                entries.Add(new XboxEntry{Name=name,Directory=(bytes[pos+1]&16)!=0,First=U32(bytes,pos+44),Size=U32(bytes,pos+48),Slot=Sector(id)*512+pos});

            }

        }return entries;

    }

    public long FreeBytes {get{long count=0;for(uint id=1;id<=volume.Count;id++)if(GetFat(id)==0)count++;return count*volume.ClusterBytes;}}

    public void GuardWrites(Action<string,long,long> progress){

        if(faulted)throw new IOException("A previous write failed. Reopen the HDD before editing again.");Storage.Recheck(target);if(ownershipChecked)return;

        progress("Checking directory ownership before editing",0,0);var used=new HashSet<uint>();int entries=0;

        Action<XboxEntry,int> visit=null;visit=(entry,depth)=>{

            token.ThrowIfCancellationRequested();if(depth>64||++entries>2000000)throw new IOException("Directory tree is too deep or damaged.");

            foreach(uint id in Chain(entry))if(!used.Add(id))throw new IOException("Cross-linked files found. Editing blocked.");

            if(entry.Directory)foreach(var child in List(entry))visit(child,depth+1);

        };visit(Root,0);ownershipChecked=true;

    }

    void Metadata(long byteOffset,byte[] bytes,CancellationToken? commitToken=null){

        var ioToken=commitToken??token;

        long sector=byteOffset/512;int within=(int)(byteOffset%512);int size=((within+bytes.Length+511)/512)*512;

        var block=disk.Read(sector,size,ioToken);Array.Copy(bytes,0,block,within,bytes.Length);disk.Write(sector,block,ioToken,false);disk.Flush();

        if(!disk.Read(sector,size,ioToken).SequenceEqual(block))throw new IOException("Metadata read-back mismatch. Stop editing this drive.");

    }

    void PersistFat(IEnumerable<uint> changed,CancellationToken? commitToken=null){

        var ioToken=commitToken??token;

        var sectors=changed.Select(id=>(long)id*volume.Width/512).Distinct().OrderBy(x=>x).ToArray();

        for(int at=0;at<sectors.Length;){int end=at+1;while(end<sectors.Length&&sectors[end]==sectors[end-1]+1&&end-at<2048)end++;

            var bytes=new byte[(end-at)*512];Array.Copy(fat,sectors[at]*512,bytes,0,bytes.Length);disk.Write(volume.Start+8+sectors[at],bytes,ioToken,false);disk.Flush();

            if(!disk.Read(volume.Start+8+sectors[at],bytes.Length,ioToken).SequenceEqual(bytes))throw new IOException("FAT read-back mismatch. Stop editing this drive.");at=end;

        }

    }

    List<uint> Allocate(int count){

        var allocated=new List<uint>();if(count==0)return allocated;

        for(int pass=0;pass<2&&allocated.Count<count;pass++){uint first=pass==0?nextFree:1;long last=pass==0?volume.Count:nextFree-1;

            for(uint id=first;id<=last&&allocated.Count<count;id++){token.ThrowIfCancellationRequested();if(GetFat(id)==0)allocated.Add(id);}

        }

        if(allocated.Count!=count)throw new IOException("Not enough free space.");

        for(int i=0;i<count;i++)SetFat(allocated[i],i+1<count?allocated[i+1]:End);nextFree=allocated[count-1]==volume.Count?1:allocated[count-1]+1;return allocated;

    }

    void Free(List<uint> chain){foreach(uint id in chain)SetFat(id,0);PersistFat(chain);if(chain.Count>0)nextFree=Math.Min(nextFree,chain.Min());}

    long FindSlot(XboxEntry folder){

        var chain=Chain(folder);foreach(uint id in chain){var bytes=disk.Read(Sector(id),volume.ClusterBytes,token);for(int pos=0;pos<bytes.Length;pos+=64)if(bytes[pos]==0||bytes[pos]==255||bytes[pos]==0xE5)return Sector(id)*512+pos;}

        var added=Allocate(1);uint last=chain.Last();try{

            disk.Write(Sector(added[0]),Enumerable.Repeat((byte)255,volume.ClusterBytes).ToArray(),token,false);disk.Flush();PersistFat(added);SetFat(last,added[0]);PersistFat(new[]{last});return Sector(added[0])*512;

        }catch{ownershipChecked=false;faulted=true;throw;}

    }

    static void ValidateName(string name){FatxNames.Validate(name);}

    static void Put(byte[] b,int at,uint value){Array.Copy(BitConverter.GetBytes(value),0,b,at,4);}

    byte[] DirectoryRecord(XboxEntry item){

        var encodedName=FatxNames.Encode(item.Name);var bytes=new byte[64];bytes[0]=(byte)encodedName.Length;bytes[1]=(byte)(item.Directory?16:32);Array.Copy(encodedName,0,bytes,2,encodedName.Length);Put(bytes,44,item.First);Put(bytes,48,item.Directory?0:item.Size);

        var now=DateTime.Now;ushort date=(ushort)(((Math.Max(1980,Math.Min(2107,now.Year))-1980)<<9)|(now.Month<<5)|now.Day),time=(ushort)((now.Hour<<11)|(now.Minute<<5)|(now.Second/2));

        for(int off=52;off<64;off+=4){Array.Copy(BitConverter.GetBytes(time),0,bytes,off,2);Array.Copy(BitConverter.GetBytes(date),0,bytes,off+2,2);}return bytes;

    }

    void Publish(XboxEntry item,long slot){Metadata(slot,DirectoryRecord(item));item.Slot=slot;}

    public XboxEntry NewFolder(XboxEntry parent,string name){

        ValidateName(name);if(List(parent).Any(x=>String.Equals(x.Name,name,StringComparison.OrdinalIgnoreCase)))throw new IOException("That name already exists.");

        long slot=FindSlot(parent);var chain=Allocate(1);bool published=false;

        try{disk.Write(Sector(chain[0]),Enumerable.Repeat((byte)255,volume.ClusterBytes).ToArray(),token,false);disk.Flush();PersistFat(chain);var folder=new XboxEntry{Name=name,Directory=true,First=chain[0]};published=true;Publish(folder,slot);return folder;}

        catch{ownershipChecked=false;faulted=true;throw;}

        finally{if(!published){try{Free(chain);}catch{ownershipChecked=false;}}}

    }

    internal List<ExportItem> ExportItems(IEnumerable<XboxEntry> selected,Action<string,long,long> progress=null){
        var items=new List<ExportItem>();var paths=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        Action<XboxEntry,string,int> visit=null;visit=(entry,parent,depth)=>{token.ThrowIfCancellationRequested();if(depth>64)throw new IOException("Directory tree too deep.");string name=parent.Length==0?entry.Name:parent+"\\"+entry.Name;if(name.Length>=260||!paths.Add(name))throw new IOException("Windows drag path is too long or duplicated. Use Copy to PC instead.");items.Add(new ExportItem{Name=name,Entry=entry,Chain=Chain(entry)});if(progress!=null)progress("Preparing "+name,items.Count,0);if(entry.Directory)foreach(var child in List(entry))visit(child,name,depth+1);};foreach(var entry in selected)visit(entry,"",0);return items;
    }
    internal int ReadExport(ExportItem item,long position,byte[] output,int count){
        token.ThrowIfCancellationRequested();if(position<0||count<0||count>output.Length||item.Entry.Directory)throw new IOException("Invalid export read.");int total=(int)Math.Min(count,Math.Max(0,(long)item.Entry.Size-position));int copied=0;
        while(copied<total){token.ThrowIfCancellationRequested();int index=checked((int)(position/volume.ClusterBytes)),within=(int)(position%volume.ClusterBytes);if(index>=item.Chain.Count)throw new IOException("Short export chain.");int clusters=1;int max=Math.Max(1,1024*1024/volume.ClusterBytes);while(index+clusters<item.Chain.Count&&clusters<max&&item.Chain[index+clusters]==item.Chain[index+clusters-1]+1&&(long)clusters*volume.ClusterBytes-within<total-copied)clusters++;byte[] bytes;lock(disk){bytes=disk.Read(Sector(item.Chain[index]),checked(clusters*volume.ClusterBytes),token);}int n=Math.Min(total-copied,bytes.Length-within);Array.Copy(bytes,within,output,copied,n);copied+=n;position+=n;}return copied;
    }

    public void Export(IEnumerable<XboxEntry> selection,string destination,Action<string,long,long> progress){

        progress("Checking PC destination and preparing file list...",0,0);
        Storage.ProtectSources(target,new InstallPlan{CFolder=destination});var jobs=new List<Tuple<XboxEntry,string,List<uint>>>();var directories=new List<string>();long scannedBytes=0;int scanned=0;

        Action<XboxEntry,string,int> collect=null;collect=(entry,path,depth)=>{token.ThrowIfCancellationRequested();if(depth>64)throw new IOException("Directory tree too deep.");

            scanned++;progress("Scanning "+scanned.ToString("N0")+" entries | "+jobs.Count.ToString("N0")+" files | "+(scannedBytes/1000000.0).ToString("0.0")+" MB found | "+entry.Name,0,0);
            string file=Path.Combine(path,entry.Name);if(File.Exists(file)||Directory.Exists(file))throw new IOException("Destination already exists: "+file);

            if(entry.Directory){directories.Add(file);foreach(var child in List(entry))collect(child,file,depth+1);}else{jobs.Add(Tuple.Create(entry,file,Chain(entry)));scannedBytes+=entry.Size;}

        };foreach(var entry in selection)collect(entry,destination,0);

        long total=jobs.Sum(j=>(long)j.Item1.Size),done=0;int made=0;foreach(string dir in directories){token.ThrowIfCancellationRequested();progress("Preparing PC folders "+made.ToString("N0")+" / "+directories.Count.ToString("N0"),0,0);Directory.CreateDirectory(dir);made++;}int count=0;progress("Starting copy | "+jobs.Count.ToString("N0")+" files | "+(total/1000000.0).ToString("0.0")+" MB",0,total);

        using(var linked=CancellationTokenSource.CreateLinkedTokenSource(token))using(var queue=new BlockingCollection<ExportBlock>(4)){
            Exception readFailure=null;
            // This worker alone reads the raw disk; the consumer only writes PC files.
            var reader=Task.Factory.StartNew(()=>{
                try{for(int jobIndex=0;jobIndex<jobs.Count;jobIndex++){
                    linked.Token.ThrowIfCancellationRequested();var job=jobs[jobIndex];long left=job.Item1.Size;var chain=job.Item3;int maxClusters=Math.Max(1,1024*1024/volume.ClusterBytes);
                    if(left==0){queue.Add(new ExportBlock{Job=jobIndex,Bytes=new byte[0],Last=true},linked.Token);continue;}
                    for(int at=0;at<chain.Count&&left>0;){linked.Token.ThrowIfCancellationRequested();int end=at+1;while(end<chain.Count&&end-at<maxClusters&&chain[end]==chain[end-1]+1)end++;var bytes=disk.Read(Sector(chain[at]),checked((end-at)*volume.ClusterBytes),linked.Token);int length=(int)Math.Min(left,bytes.Length);left-=length;queue.Add(new ExportBlock{Job=jobIndex,Bytes=bytes,Length=length,Last=left==0},linked.Token);at=end;}
                    if(left!=0)throw new IOException("Source file chain ended early.");
                }}catch(Exception e){readFailure=e;}finally{queue.CompleteAdding();}
            },CancellationToken.None,TaskCreationOptions.LongRunning,TaskScheduler.Default);
            FileStream output=null;string activePath=null;int activeJob=-1;bool created=false,complete=false;
            try{
                foreach(var block in queue.GetConsumingEnumerable(linked.Token)){
                    token.ThrowIfCancellationRequested();var job=jobs[block.Job];
                    if(output==null){if(block.Job!=count)throw new IOException("Unexpected export file order.");activePath=job.Item2;created=false;complete=false;activeJob=block.Job;output=new FileStream(activePath,FileMode.CreateNew,FileAccess.Write,FileShare.None,65536,FileOptions.SequentialScan);created=true;}
                    if(activeJob!=block.Job)throw new IOException("Export file ended unexpectedly.");
                    output.Write(block.Bytes,0,block.Length);done+=block.Length;progress("Copying out "+job.Item1.Name+" | Files "+count+" / "+jobs.Count,done,total);
                    if(block.Last){output.Flush();output.Dispose();output=null;complete=true;count++;progress("Copied out "+count+" / "+jobs.Count+" files",done,total);}
                }
                reader.GetAwaiter().GetResult();token.ThrowIfCancellationRequested();if(readFailure!=null)throw new IOException("Source read failed during export.",readFailure);if(output!=null||count!=jobs.Count)throw new IOException("Export did not finish every file.");
            }finally{
                try{if(output!=null)output.Dispose();}finally{linked.Cancel();reader.GetAwaiter().GetResult();if(created&&!complete&&activePath!=null&&File.Exists(activePath))File.Delete(activePath);}
            }
        }

    }
    sealed class ExportBlock {public int Job,Length;public byte[] Bytes;public bool Last;}

    sealed class Source {public string Path,Name;public bool Directory;public long Size,SourceSize;public byte[] PatchedData;public XisoImage Image;public Func<Stream> OpenSource;public DateTime Stamp;public List<Source> Children=new List<Source>();}

    Source ImageSource(XisoImport image){

        Func<XisoImport.Item,Source> convert=null;convert=item=>{var s=new Source{Name=item.Name,Directory=item.Directory,Size=item.Size,PatchedData=item.Data,OpenSource=item.Open};foreach(var child in item.Children)s.Children.Add(convert(child));return s;};

        if(image.Game!=null)importFixes.Add(image.Game.Description);return convert(image.Root);

    }

    Source ScanSource(string path,int depth,GameCompatibility game=null,Func<GameCompatibility,bool> approval=null,bool looseGame=false){

        token.ThrowIfCancellationRequested();if(!looseGame&&game==null&&File.Exists(path)&&XisoReader.Candidate(path))return ImageSource(new XisoImport(path,approval,token));if(depth>64)throw new IOException("Source is too deep.");var attributes=File.GetAttributes(path);if((attributes&FileAttributes.ReparsePoint)!=0)throw new IOException("Links/junctions are not supported.");var source=new Source{Path=path,Name=Path.GetFileName(path.TrimEnd('\\')),Directory=(attributes&FileAttributes.Directory)!=0};if(source.Directory){var found=GameCompatibility.ForFolder(path,approval,token);if(found!=null){game=found;if(found.FolderName!=null)source.Name=found.FolderName;importFixes.Add(found.Description);}}if(game!=null&&!path.Equals(game.Root,StringComparison.OrdinalIgnoreCase))source.Name=game.Name(path);ValidateName(source.Name);

        if(source.Directory&&game!=null&&game.Image!=null&&path.Equals(game.Root,StringComparison.OrdinalIgnoreCase)){

            source.Children.Add(new Source{Name="game.iso",Image=game.Image,Size=game.Image.Length});

            source.Children.Add(new Source{Name="default.xbe",PatchedData=game.ImageLauncher,Size=game.ImageLauncher.LongLength});return source;

        }

        if(source.Directory){var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);foreach(string child in Directory.EnumerateFileSystemEntries(path)){if(game!=null&&File.Exists(child)&&game.Skip(child))continue;var scanned=ScanSource(child,depth+1,game,approval,looseGame||File.Exists(Path.Combine(path,"default.xbe")));if(!names.Add(scanned.Name))throw new IOException("Case-insensitive source-name collision.");source.Children.Add(scanned);}}else{var info=new FileInfo(path);source.SourceSize=source.Size=info.Length;source.Stamp=info.LastWriteTimeUtc;if(game!=null)source.PatchedData=game.Replacement(path);if(source.PatchedData!=null)source.Size=source.PatchedData.LongLength;if(source.Size>uint.MaxValue)throw new IOException("FATX files must be smaller than 4 GiB.");}return source;

    }

    public void Import(XboxEntry parent,IEnumerable<string> paths,Action<string,long,long> progress,Func<GameCompatibility,bool> approval=null,Action confirmPatches=null){

        importFixes.Clear();var roots=new List<Source>();foreach(string path in paths){Storage.ProtectSources(target,new InstallPlan{CFolder=Directory.Exists(path)?path:Path.GetDirectoryName(path)});roots.Add(ScanSource(path,0,null,approval));}

        long total=0,required=0;int totalFiles=0;var existing=new HashSet<string>(List(parent).Select(x=>x.Name),StringComparer.OrdinalIgnoreCase);

        Action<Source> count=null;count=s=>{if(s.Directory){required+=Math.Max(1,(s.Children.Count+1L)*64/volume.ClusterBytes+1)*volume.ClusterBytes;foreach(var child in s.Children)count(child);}else{total+=s.Size;required+=((s.Size+volume.ClusterBytes-1)/volume.ClusterBytes)*volume.ClusterBytes;totalFiles++;}};

        foreach(var root in roots){if(!existing.Add(root.Name))throw new IOException("Name already exists; nothing overwritten: "+root.Name);count(root);}required+=volume.ClusterBytes*((roots.Count*64L+volume.ClusterBytes-1)/volume.ClusterBytes+1);

        if(required>FreeBytes)throw new IOException("Not enough free space.");if(confirmPatches!=null)confirmPatches();token.ThrowIfCancellationRequested();GuardWrites(progress);long done=0;int completed=0;

        foreach(var source in roots)ImportTree(parent,source,total,totalFiles,ref done,ref completed,progress);

    }

    sealed class ImportNode {public Source Source;public XboxEntry Entry;public List<uint> Chain;public List<ImportNode> Children=new List<ImportNode>();}

    sealed class ImportWriter:IDisposable {

        readonly BlockingCollection<Tuple<long,byte[]>> queue=new BlockingCollection<Tuple<long,byte[]>>(2);

        readonly CancellationTokenSource linked;readonly Task worker;readonly CancellationToken userCancel;

        volatile Exception failure;bool completed;

        public ImportWriter(RawDisk disk,CancellationToken cancel){

            userCancel=cancel;linked=CancellationTokenSource.CreateLinkedTokenSource(cancel);

            worker=Task.Factory.StartNew(()=>{try{foreach(var block in queue.GetConsumingEnumerable(linked.Token))disk.Write(block.Item1,block.Item2,linked.Token,false);}catch(Exception e){failure=e;linked.Cancel();}},CancellationToken.None,TaskCreationOptions.LongRunning,TaskScheduler.Default);

        }

        public void Write(long sector,byte[] bytes){try{queue.Add(Tuple.Create(sector,bytes),linked.Token);}catch(OperationCanceledException){if(failure!=null&&!userCancel.IsCancellationRequested)throw new IOException("Destination write failed.",failure);throw;}}

        public void Complete(){queue.CompleteAdding();worker.Wait();completed=true;if(failure!=null)throw failure;userCancel.ThrowIfCancellationRequested();}

        public void Dispose(){if(!completed)linked.Cancel();queue.CompleteAdding();worker.Wait();queue.Dispose();linked.Dispose();}

    }

    void ImportTree(XboxEntry parent,Source source,long total,int totalFiles,ref long done,ref int completed,Action<string,long,long> progress){

        // Only new, unpublished clusters are batched. Existing files and directory

        // metadata retain their verified commit path.

        var nodes=new List<ImportNode>();var allocated=new List<uint>();bool fatStarted=false,publishing=false;

        long copied=done;int filesCopied=completed;

        try{

            Func<Source,ImportNode> plan=null;plan=s=>{

                token.ThrowIfCancellationRequested();long bytes=s.Directory?(s.Children.Count+1L)*64:s.Size;

                var chain=Allocate(checked((int)((bytes+volume.ClusterBytes-1)/volume.ClusterBytes)));allocated.AddRange(chain);

                var node=new ImportNode{Source=s,Chain=chain,Entry=new XboxEntry{Name=s.Name,Directory=s.Directory,First=chain.Count==0?0:chain[0],Size=s.Directory?0:(uint)s.Size}};nodes.Add(node);

                foreach(var child in s.Children)node.Children.Add(plan(child));return node;

            };

            var root=plan(source);var directoryBlocks=new List<Tuple<long,byte[]>>();

            using(var writer=new ImportWriter(disk,token)){

            const int batchSize=1024*1024;var batch=new byte[batchSize];int used=0;long batchSector=0;

            Action flush=()=>{if(used==0)return;byte[] write=batch;if(used!=batchSize){write=new byte[used];Array.Copy(batch,write,used);}writer.Write(batchSector,write);batch=new byte[batchSize];used=0;};

            Action<uint,byte[]> append=(id,bytes)=>{

                long sector=Sector(id);if(used!=0&&(sector!=batchSector+used/512||used+bytes.Length>batchSize))flush();

                if(used==0)batchSector=sector;Array.Copy(bytes,0,batch,used,bytes.Length);used+=bytes.Length;if(used==batchSize)flush();

            };

            var buffer=new byte[volume.ClusterBytes];

            foreach(var node in nodes){

                token.ThrowIfCancellationRequested();var s=node.Source;

                if(s.Directory){

                    int index=0;foreach(uint id in node.Chain){

                        var bytes=Enumerable.Repeat((byte)255,volume.ClusterBytes).ToArray();

                        for(int offset=0;offset<bytes.Length&&index<node.Children.Count;offset+=64,index++)Array.Copy(DirectoryRecord(node.Children[index].Entry),0,bytes,offset,64);

                        append(id,bytes);using(var sha=System.Security.Cryptography.SHA256.Create())directoryBlocks.Add(Tuple.Create(Sector(id),sha.ComputeHash(bytes)));

                    }continue;

                }

                if(s.Path!=null){var info=new FileInfo(s.Path);if((info.Attributes&FileAttributes.ReparsePoint)!=0||info.Length!=s.SourceSize||info.LastWriteTimeUtc!=s.Stamp)throw new IOException("Source changed: "+s.Name);}

                using(var input=s.OpenSource!=null?s.OpenSource():s.Image!=null?s.Image.Open():s.PatchedData!=null?(Stream)new MemoryStream(s.PatchedData,false):new FileStream(s.Path,FileMode.Open,FileAccess.Read,FileShare.Read,s.Size<1024*1024?65536:1024*1024,FileOptions.SequentialScan)){

                    if(input.Length!=s.Size)throw new IOException("Source changed: "+s.Name);long left=s.Size;

                    foreach(uint id in node.Chain){

                        token.ThrowIfCancellationRequested();Array.Clear(buffer,0,buffer.Length);int wanted=(int)Math.Min(left,buffer.Length),got=0;

                        while(got<wanted){int n=input.Read(buffer,got,wanted-got);if(n==0)throw new IOException("Short source read: "+s.Name);got+=n;}

                        append(id,buffer);left-=got;copied+=got;progress("Adding "+s.Name+" | Files "+filesCopied+" / "+totalFiles,copied,total);

                    }

                }

                if(s.Path!=null&&File.GetLastWriteTimeUtc(s.Path)!=s.Stamp)throw new IOException("Source changed: "+s.Name);filesCopied++;

            }

            flush();writer.Complete();

            }

            progress("Saving "+source.Name+" and checking directory metadata",copied,total);disk.Flush();

            // Verify new directory contents before making any root visible.

            using(var sha=System.Security.Cryptography.SHA256.Create())foreach(var block in directoryBlocks)if(!sha.ComputeHash(disk.Read(block.Item1,volume.ClusterBytes,token)).SequenceEqual(block.Item2))throw new IOException("New directory read-back mismatch.");

            fatStarted=true;PersistFat(allocated);

            long slot=FindSlot(parent);token.ThrowIfCancellationRequested();publishing=true;Publish(root.Entry,slot);

            done=copied;completed=filesCopied;progress("Added "+source.Name+" | Files "+completed+" / "+totalFiles,done,total);

        }catch{

            // Before publication, reclaim only this operation's new clusters.

            // If publication started, preserve chains because its entry may exist.

            if(!publishing){try{foreach(uint id in allocated)SetFat(id,0);if(fatStarted)PersistFat(allocated,CancellationToken.None);if(allocated.Count>0)nextFree=Math.Min(nextFree,allocated.Min());}catch{faulted=true;}}

            ownershipChecked=false;faulted=true;throw;

        }

    }

    sealed class DeleteJob {public XboxEntry Entry;public List<uint> Chain;}
    void CommitDeleteBatch(List<DeleteJob> batch){
        // Once a bounded commit starts, finish it before honouring cancellation.
        // Never free clusters while their directory entries may still be live.
        var commit=CancellationToken.None;var records=new SortedDictionary<long,byte[]>();
        foreach(var job in batch){long sector=job.Entry.Slot/512;byte[] bytes;if(!records.TryGetValue(sector,out bytes)){bytes=disk.Read(sector,512,commit);records.Add(sector,bytes);}int offset=(int)(job.Entry.Slot%512);Array.Clear(bytes,offset,64);bytes[offset]=0xe5;}
        foreach(var record in records)disk.Write(record.Key,record.Value,commit,false);
        disk.Flush();
        foreach(var record in records)if(!disk.Read(record.Key,512,commit).SequenceEqual(record.Value))throw new IOException("Delete directory read-back mismatch. Reopen the drive before continuing.");
        var freed=new List<uint>();foreach(var job in batch)foreach(uint id in job.Chain){SetFat(id,0);freed.Add(id);}
        var sectors=freed.Select(id=>(long)id*volume.Width/512).Distinct().OrderBy(x=>x).ToArray();var ranges=new List<Tuple<long,int>>();
        for(int at=0;at<sectors.Length;){int end=at+1;while(end<sectors.Length&&sectors[end]==sectors[end-1]+1&&end-at<2048)end++;int size=(end-at)*512;var bytes=new byte[size];Array.Copy(fat,sectors[at]*512,bytes,0,size);disk.Write(volume.Start+8+sectors[at],bytes,commit,false);ranges.Add(Tuple.Create(sectors[at],size));at=end;}
        if(ranges.Count>0){disk.Flush();foreach(var range in ranges){var expected=new byte[range.Item2];Array.Copy(fat,range.Item1*512,expected,0,expected.Length);if(!disk.Read(volume.Start+8+range.Item1,expected.Length,commit).SequenceEqual(expected))throw new IOException("Delete allocation-table read-back mismatch. Reopen the drive before continuing.");}}
        if(freed.Count>0)nextFree=Math.Min(nextFree,freed.Min());
    }
    public void Delete(IEnumerable<XboxEntry> selected,Action<string,long,long> progress){
        GuardWrites(progress);var jobs=new List<DeleteJob>();var slots=new HashSet<long>();
        Action<XboxEntry,int> collect=null;collect=(entry,depth)=>{token.ThrowIfCancellationRequested();if(entry.Slot<0||depth>64)throw new IOException("Cannot delete a partition root or damaged directory tree.");if(!slots.Add(entry.Slot))throw new IOException("Repeated delete selection or directory entry.");if(entry.Directory)foreach(var child in List(entry))collect(child,depth+1);jobs.Add(new DeleteJob{Entry=entry,Chain=Chain(entry)});};
        progress("Counting items to delete",0,0);foreach(var entry in selected)collect(entry,0);
        int done=0;try{while(done<jobs.Count){token.ThrowIfCancellationRequested();var batch=jobs.GetRange(done,Math.Min(256,jobs.Count-done));progress("Deleting "+batch[0].Entry.Name+" | Items "+done+" / "+jobs.Count,done,jobs.Count);CommitDeleteBatch(batch);done+=batch.Count;progress("Deleted "+batch[batch.Count-1].Entry.Name+" | Items "+done+" / "+jobs.Count,done,jobs.Count);}}
        catch{faulted=true;ownershipChecked=false;throw;}
    }

    public string PartitionKey {get{return volume.Name;}}

    bool ContainsFolder(XboxEntry source,uint destination,int depth){

        if(depth>64)throw new IOException("Folder tree too deep.");if(!source.Directory)return false;if(source.First==destination)return true;

        foreach(var child in List(source).Where(x=>x.Directory))if(ContainsFolder(child,destination,depth+1))return true;return false;

    }

    XboxEntry[] TransferSelection(XboxEntry sourceParent,IEnumerable<XboxEntry> selection,XboxEntry destination){

        if(!destination.Directory)throw new IOException("Drop onto a folder.");

        var actual=List(sourceParent);var items=selection.ToArray();var names=new HashSet<string>(List(destination).Select(x=>x.Name),StringComparer.OrdinalIgnoreCase);

        foreach(var item in items){if(item.Slot<0||!actual.Any(x=>x.Slot==item.Slot&&x.First==item.First&&x.Size==item.Size&&x.Name==item.Name&&x.Directory==item.Directory))throw new IOException("The selected item changed. Refresh and select it again.");

            if(!names.Add(item.Name))throw new IOException("Destination already contains "+item.Name+". Nothing overwritten.");if(ContainsFolder(item,destination.First,0))throw new IOException("Cannot put a folder inside itself.");

        }return items;

    }

    public void MoveWithin(XboxEntry sourceParent,IEnumerable<XboxEntry> selection,XboxEntry destination,Action<string,long,long> progress){

        GuardWrites(progress);if(sourceParent.First==destination.First)return;var items=TransferSelection(sourceParent,selection,destination);int done=0;

        foreach(var item in items){token.ThrowIfCancellationRequested();long newSlot=FindSlot(destination);var sector=disk.Read(item.Slot/512,512,token);var original=new byte[64];Array.Copy(sector,item.Slot%512,original,0,64);var removed=new byte[64];removed[0]=0xE5;

            // Each move commits directory entries without freeing or copying file data.

            // Cancellation waits until this brief metadata commit/rollback has finished.

            try{Metadata(newSlot,original,CancellationToken.None);Metadata(item.Slot,removed,CancellationToken.None);}

            catch{faulted=true;ownershipChecked=false;try{Metadata(item.Slot,original,CancellationToken.None);Metadata(newSlot,removed,CancellationToken.None);}catch{throw new IOException("Move interrupted; directory recovery is required. File data has not been freed.");}throw;}

            done++;progress("Moving "+item.Name+" | Items "+done+" / "+items.Length,done,items.Length);

        }

    }

    sealed class DiskSource {public XboxEntry Entry;public List<DiskSource> Children=new List<DiskSource>();}

    public void CopyWithin(XboxEntry sourceParent,IEnumerable<XboxEntry> selection,XboxEntry destination,Action<string,long,long> progress){

        GuardWrites(progress);var items=TransferSelection(sourceParent,selection,destination);long total=0,required=0,done=0;int totalFiles=0,completed=0;

        Func<XboxEntry,int,DiskSource> collect=null;collect=(entry,depth)=>{token.ThrowIfCancellationRequested();if(depth>64)throw new IOException("Folder tree too deep.");var node=new DiskSource{Entry=entry};

            if(entry.Directory){foreach(var child in List(entry))node.Children.Add(collect(child,depth+1));required+=Math.Max(1,(node.Children.Count+1L)*64/volume.ClusterBytes+1)*volume.ClusterBytes;}

            else{Chain(entry);total+=entry.Size;required+=((entry.Size+(long)volume.ClusterBytes-1)/volume.ClusterBytes)*volume.ClusterBytes;totalFiles++;}return node;

        };var sources=items.Select(x=>collect(x,0)).ToArray();required+=volume.ClusterBytes*((items.Length*64L+volume.ClusterBytes-1)/volume.ClusterBytes+1);if(required>FreeBytes)throw new IOException("Not enough free space.");

        Action<DiskSource,XboxEntry> copy=null;copy=(node,parent)=>{token.ThrowIfCancellationRequested();if(node.Entry.Directory){var folder=NewFolder(parent,node.Entry.Name);foreach(var child in node.Children)copy(child,folder);return;}

            long slot=FindSlot(parent);var sourceChain=Chain(node.Entry);int count=checked((int)((node.Entry.Size+(long)volume.ClusterBytes-1)/volume.ClusterBytes));var allocated=Allocate(count);bool publishing=false;

            try{long left=node.Entry.Size;for(int i=0;i<count;i++){token.ThrowIfCancellationRequested();var bytes=disk.Read(Sector(sourceChain[i]),volume.ClusterBytes,token);int length=(int)Math.Min(left,volume.ClusterBytes);if(length<bytes.Length)Array.Clear(bytes,length,bytes.Length-length);disk.Write(Sector(allocated[i]),bytes,token,false);left-=length;done+=length;progress("Copying "+node.Entry.Name+" | Files "+completed+" / "+totalFiles,done,total);}

                disk.Flush();PersistFat(allocated);publishing=true;Publish(new XboxEntry{Name=node.Entry.Name,First=count==0?0:allocated[0],Size=node.Entry.Size},slot);completed++;progress("Copied "+completed+" / "+totalFiles+" files",done,total);

            }catch{faulted=true;ownershipChecked=false;throw;}finally{if(!publishing){try{Free(allocated);}catch{faulted=true;}}}

        };foreach(var node in sources)copy(node,destination);

    }



    public string CheckIntegrity(Action<string,long,long> progress){

        var report=new StringBuilder();int errors=0,warnings=0,printed=0;bool complete=true;

        report.AppendLine("RRFF read-only FATX filesystem check");report.AppendLine("Started: "+DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"));

        report.AppendLine("This checks filesystem metadata; it is not a SMART test or a full file-content/media scan.");

        Action<string,bool> issue=(message,error)=>{if(error)errors++;else warnings++;if(printed++<2000)report.AppendLine((error?"ERROR: ":"WARNING: ")+message);};

        foreach(string message in headerIssues)issue(message,true);

        foreach(var partition in Partitions){

            token.ThrowIfCancellationRequested();report.AppendLine();report.AppendLine("Partition "+partition.Name+":");

            try{

                Select(partition);var owners=new Dictionary<uint,int>();var directoryRoots=new HashSet<uint>();long files=0,folders=0,free=0,allocated=0,orphaned=0,bad=0;int owner=0;bool partitionComplete=true;

                Action<XboxEntry,string,int> visit=null;visit=(entry,path,depth)=>{

                    token.ThrowIfCancellationRequested();if(depth>64||owner>=2000000){issue(path+": inspection limit exceeded",true);partitionComplete=false;return;}int current=++owner;

                    if(entry.Directory)folders++;else files++;

                    var chain=new HashSet<uint>();uint id=entry.First;long clusters=0;bool valid=true;

                    if(id==0){if(entry.Directory||entry.Size!=0){issue(path+": missing cluster chain",true);valid=false;}}

                    while(id!=0){

                        token.ThrowIfCancellationRequested();if(id<1||id>volume.Count){issue(path+": cluster is outside partition",true);valid=false;break;}

                        if(!chain.Add(id)){issue(path+": looping cluster chain",true);valid=false;break;}

                        int previous;if(owners.TryGetValue(id,out previous)){issue(path+": cluster "+id+" is shared with another entry (cross-link)",true);}else owners.Add(id,current);

                        clusters++;uint next=GetFat(id);if(next>=(volume.Width==4?0xFFFFFFF8u:0xFFF8u))break;

                        if(next==0){issue(path+": file chain points to a free cluster",true);valid=false;break;}

                        id=next;

                    }

                    if(!entry.Directory){long needed=(entry.Size+(long)volume.ClusterBytes-1)/volume.ClusterBytes;if(clusters<needed)issue(path+": file size exceeds allocated chain",true);else if(clusters>needed)issue(path+": extra allocated clusters beyond file size",false);}

                    if(entry.Directory){if(!valid||!directoryRoots.Add(entry.First)){partitionComplete=false;return;}

                        try{foreach(var child in List(entry))visit(child,path+"\\"+child.Name,depth+1);}catch(OperationCanceledException){throw;}catch(Exception e){partitionComplete=false;issue(path+": directory could not be fully read: "+e.Message,true);}

                    }

                    if(owner%128==0)progress("Checking "+partition.Name+": directories and file chains | "+files.ToString("N0")+" files",0,0);

                };visit(Root,partition.Name+":",0);

                for(uint id=1;id<=volume.Count;id++){

                    token.ThrowIfCancellationRequested();uint value=GetFat(id);if(value==0)free++;else if(value==(volume.Width==4?0xFFFFFFF7u:0xFFF7u))bad++;else{allocated++;if(!owners.ContainsKey(id))orphaned++;}

                    if(id%65536==0)progress("Checking "+partition.Name+": allocation map",id,volume.Count);

                }

                if(orphaned>0)issue(orphaned.ToString("N0")+(partitionComplete?" orphaned allocated clusters":" allocated clusters not accounted for; unreadable directories prevent a definite orphan count"),partitionComplete);

                if(bad>0)issue(bad.ToString("N0")+" clusters marked bad in the FAT",false);

                report.AppendLine("Files: "+files.ToString("N0")+" | Folders (including root): "+folders.ToString("N0"));

                report.AppendLine("Free: "+(free*volume.ClusterBytes/1000000000.0).ToString("0.00")+" GB | Allocated clusters: "+allocated.ToString("N0"));

                if(!partitionComplete){complete=false;report.AppendLine("Partition inspection incomplete.");}

            }catch(OperationCanceledException){throw;}catch(Exception e){complete=false;issue(partition.Name+": could not inspect partition: "+e.Message,true);}

        }

        if(printed>2000)report.AppendLine("Detail list limited to 2,000 issues; totals include all detected issues.");

        report.AppendLine();report.AppendLine("Errors: "+errors+" | Warnings: "+warnings);

        report.AppendLine(!complete?"RESULT: INCOMPLETE  -  some metadata could not be inspected.":errors>0?"RESULT: FILESYSTEM PROBLEMS FOUND.":warnings>0?"RESULT: CHECK COMPLETED WITH WARNINGS.":"RESULT: NO FATX METADATA PROBLEMS FOUND.");

        report.AppendLine("No repair or disk-write operations were performed.");return report.ToString();

    }



    public void Dispose(){disk.Dispose();}

}

}

