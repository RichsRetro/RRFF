using System;

using System.Collections.Generic;

using System.ComponentModel;

using System.IO;

using System.Linq;

using System.Management;

using System.Runtime.InteropServices;

using System.Threading;

using Microsoft.Win32.SafeHandles;



namespace RichRetro.Fatx {

internal sealed class Target {

    public int Number, Sector; public long Size; public string Model, Serial, UniqueId, Path; public bool Protected;

    public List<string> Volumes = new List<string>();

    public List<string> Mounts = new List<string>();

    public string Identity { get { return Number+"|"+Size+"|"+Model+"|"+Serial+"|"+UniqueId+"|"+Path; } }

    public override string ToString() { return "Disk "+Number+"  -  "+Model+"  -  "+(Size/1000000000.0).ToString("0.0")+" GB  -  S/N "+(Serial.Length==0?"unavailable":Serial); }

}

internal static class Storage {

    const string Scope=@"\\.\root\Microsoft\Windows\Storage";

    static bool RequiredBool(ManagementBaseObject o,string key) { if(o[key]==null)throw new IOException("Windows did not provide disk protection information."); return Convert.ToBoolean(o[key]); }

    public static List<Target> Scan() {

        var all=new List<Target>();

        using(var q=new ManagementObjectSearcher(Scope,"SELECT * FROM MSFT_Disk"))

        using(var rows=q.Get()) foreach(ManagementObject d in rows) {

            var t=new Target {Number=Convert.ToInt32(d["Number"]),Size=checked((long)Convert.ToUInt64(d["Size"])),Sector=Convert.ToInt32(d["LogicalSectorSize"]),Model=Convert.ToString(d["FriendlyName"]).Trim(),Serial=Convert.ToString(d["SerialNumber"]).Trim(),UniqueId=Convert.ToString(d["UniqueId"]),Path=Convert.ToString(d["Path"])};

            t.Protected=RequiredBool(d,"IsBoot")|RequiredBool(d,"IsSystem")|RequiredBool(d,"BootFromDisk")|RequiredBool(d,"IsClustered");

            all.Add(t);

        }

        string win=System.IO.Path.GetPathRoot(Environment.GetFolderPath(Environment.SpecialFolder.Windows));

        var sensitive=new HashSet<string>(StringComparer.OrdinalIgnoreCase); sensitive.Add(win);

        using(var q=new ManagementObjectSearcher("SELECT Name FROM Win32_PageFileUsage"))

        using(var rows=q.Get()) foreach(ManagementObject p in rows) sensitive.Add(System.IO.Path.GetPathRoot(Convert.ToString(p["Name"])));

        using(var q=new ManagementObjectSearcher(Scope,"SELECT * FROM MSFT_Partition"))

        using(var rows=q.Get()) foreach(ManagementObject p in rows) {

            var t=all.SingleOrDefault(x=>x.Number==Convert.ToInt32(p["DiskNumber"]));

            if(t==null)throw new IOException("Windows partition ownership is incomplete. Formatting is blocked.");

            t.Protected |= RequiredBool(p,"IsBoot")|RequiredBool(p,"IsSystem");

            var paths=p["AccessPaths"] as string[];

            if(paths!=null) foreach(string path in paths) {

                t.Mounts.Add(path);

                if(sensitive.Contains(path))t.Protected=true;

                if(path.StartsWith(@"\\?\Volume{",StringComparison.OrdinalIgnoreCase))t.Volumes.Add(path.TrimEnd('\\'));

            }

            var letter=Convert.ToString(p["DriveLetter"]);

            if(letter.Length==1 && sensitive.Contains(letter+@":\"))t.Protected=true;

        }

        if(!all.Any(x=>x.Protected)) throw new IOException("Windows system-disk protection could not be established. Formatting is blocked.");

        return all;

    }

    public static Target Recheck(Target expected) {

        var now=Scan().SingleOrDefault(x=>x.Number==expected.Number);

        if(now==null || now.Protected || now.Identity!=expected.Identity)throw new IOException("The selected drive changed or is now a system/boot drive. Nothing further will be written.");

        return now;

    }

    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool GetVolumePathName(string file,System.Text.StringBuilder path,uint size);

    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool GetVolumeNameForVolumeMountPoint(string mount,System.Text.StringBuilder name,uint size);

    public static void ProtectSources(Target expected,InstallPlan plan){

        Target current=Recheck(expected);

        foreach(string source in new[]{plan.CFolder,plan.EFolder,plan.FFolder,plan.GFolder}.Concat(plan.FImages.Concat(plan.GImages).Select(Path.GetDirectoryName)).Where(x=>x!=null)){

            var dir=new DirectoryInfo(source);if(!dir.Exists)throw new IOException("Source folder is missing: "+source);

            for(var parent=dir;parent!=null;parent=parent.Parent)if((parent.Attributes&FileAttributes.ReparsePoint)!=0)throw new IOException("Choose an actual local folder, not a link/junction: "+source);

            var mount=new System.Text.StringBuilder(1024);var volume=new System.Text.StringBuilder(1024);

            if(!GetVolumePathName(dir.FullName,mount,1024) || !GetVolumeNameForVolumeMountPoint(mount.ToString(),volume,1024))throw new IOException("Cannot confirm source-drive ownership. Use a local folder: "+source);

            if(current.Volumes.Contains(volume.ToString().TrimEnd('\\'),StringComparer.OrdinalIgnoreCase) || current.Mounts.Contains(mount.ToString(),StringComparer.OrdinalIgnoreCase))throw new IOException("A source folder is on the drive being erased. Choose files from another drive: "+source);

        }

    }

}

internal sealed class RawDisk:IDisposable {

    readonly List<SafeFileHandle> locks=new List<SafeFileHandle>();

    SafeFileHandle disk; readonly long size; readonly bool readOnly;

    readonly List<Written> written=new List<Written>();

    sealed class Written {public long Offset;public int Size;public byte[] Hash;}

    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern SafeFileHandle CreateFile(string n,uint access,uint share,IntPtr sa,uint mode,uint flags,IntPtr template);

    [DllImport("kernel32.dll",SetLastError=true)]static extern bool DeviceIoControl(SafeFileHandle h,uint code,IntPtr input,uint insize,byte[] output,uint outsize,out uint returned,IntPtr overlap);

    [DllImport("kernel32.dll",SetLastError=true)]static extern bool ReadFile(SafeFileHandle h,IntPtr data,uint count,IntPtr done,IntPtr overlap);

    [DllImport("kernel32.dll",SetLastError=true)]static extern bool WriteFile(SafeFileHandle h,IntPtr data,uint count,IntPtr done,IntPtr overlap);

    [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetOverlappedResult(SafeFileHandle h,IntPtr overlap,out uint count,bool wait);

    [DllImport("kernel32.dll",SetLastError=true)]static extern bool CancelIoEx(SafeFileHandle h,IntPtr overlap);

    [DllImport("kernel32.dll",SetLastError=true)]static extern bool FlushFileBuffers(SafeFileHandle h);

    [StructLayout(LayoutKind.Sequential)]struct Overlap {public IntPtr Internal,InternalHigh;public uint Offset,OffsetHigh;public IntPtr Event;}

    static readonly List<object> unfinished=new List<object>();

    IntPtr ioBuffer,ioOverlap;ManualResetEvent ioSignal;bool ioAbandoned;

    void Transfer(bool write,long offset,byte[] bytes,int index,int count,CancellationToken cancel) {

        if(count<=0 || count>65536 || count%512!=0 || offset%512!=0 || offset<0 || offset>size-count)throw new IOException("Invalid bounded disk request.");

        if(ioAbandoned)throw new IOException("A previous disk request did not finish; the connection must be restored before retrying.");

        if(ioSignal==null){ioBuffer=Marshal.AllocHGlobal(65536);ioOverlap=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(Overlap)));ioSignal=new ManualResetEvent(false);}

        IntPtr data=ioBuffer,ov=ioOverlap;var signal=ioSignal;signal.Reset();bool pending=false,release=true;

        try {

            cancel.ThrowIfCancellationRequested();

            if(write)Marshal.Copy(bytes,index,data,count);

            Marshal.StructureToPtr(new Overlap{Offset=(uint)offset,OffsetHigh=(uint)((ulong)offset>>32),Event=signal.SafeWaitHandle.DangerousGetHandle()},ov,false);

            bool ok=write?WriteFile(disk,data,(uint)count,IntPtr.Zero,ov):ReadFile(disk,data,(uint)count,IntPtr.Zero,ov);

            if(!ok && Marshal.GetLastWin32Error()!=997)throw new Win32Exception(Marshal.GetLastWin32Error(),"Disk "+(write?"write":"read")+" failed at sector "+offset/512);

            pending=true;var started=DateTime.UtcNow;

            while(!signal.WaitOne(100)) {

                if(cancel.IsCancellationRequested || (DateTime.UtcNow-started).TotalSeconds>=30) {

                    CancelIoEx(disk,ov);

                    if(!signal.WaitOne(5000)) {

                        // Do not free memory still owned by a malfunctioning driver.

                        release=false;ioAbandoned=true;lock(unfinished)unfinished.Add(new object[]{signal,data,ov});

                    }else pending=false;

                    if(cancel.IsCancellationRequested)throw new OperationCanceledException(cancel);

                    throw new IOException("The storage connection stopped responding during a "+(write?"write":"read")+" at sector "+offset/512+". Formatting stopped; check the adapter, cable and power before retrying.");

                }

            }

            uint transferred;

            bool complete=GetOverlappedResult(disk,ov,out transferred,false);pending=false;

            if(!complete)throw new Win32Exception(Marshal.GetLastWin32Error(),"Disk request failed at sector "+offset/512);

            if(transferred!=count)throw new IOException("Short disk transfer at sector "+offset/512);

            if(!write)Marshal.Copy(data,bytes,index,count);

        } finally {

            if(pending && release){CancelIoEx(disk,ov);if(!signal.WaitOne(5000)){release=false;ioAbandoned=true;lock(unfinished)unfinished.Add(new object[]{signal,data,ov});}}

        }

    }

    public RawDisk(Target target,bool readOnly=false) {

        this.readOnly=readOnly;

        size=target.Size;

        try {

            Target fresh=Storage.Recheck(target);

            foreach(var volume in fresh.Volumes.Distinct(StringComparer.OrdinalIgnoreCase)) {

                var h=CreateFile(volume,0xC0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);

                if(h.IsInvalid){h.Dispose();throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot open a selected-drive volume. Close apps using it.");}

                locks.Add(h);uint returned;

                if(!DeviceIoControl(h,0x00090018,IntPtr.Zero,0,null,0,out returned,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot lock a selected-drive volume. Close apps using it; no formatting was started.");

                if(!DeviceIoControl(h,0x00090020,IntPtr.Zero,0,null,0,out returned,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot dismount the selected-drive volume.");

            }

            Storage.Recheck(target);

            var raw=CreateFile(@"\\.\PhysicalDrive"+target.Number,readOnly?0x80000000u:0xC0000000u,3,IntPtr.Zero,3,0,IntPtr.Zero);

            if(raw.IsInvalid){raw.Dispose();throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot open the selected physical drive.");}

            // Confirm actual raw-handle capacity before any write.

            byte[] length=new byte[8];uint got;

            if(!DeviceIoControl(raw,0x0007405C,IntPtr.Zero,0,length,8,out got,IntPtr.Zero) || got<8 || BitConverter.ToInt64(length,0)!=size){raw.Dispose();throw new IOException("Physical-drive capacity does not match the selected drive.");}

            // Recheck the handle's reported serial as well as the Storage API identity.

            IntPtr query=Marshal.AllocHGlobal(12);

            try {

                Marshal.Copy(new byte[12],0,query,12);var descriptor=new byte[4096];

                if(!DeviceIoControl(raw,0x002D1400,query,12,descriptor,(uint)descriptor.Length,out got,IntPtr.Zero) || got<36){raw.Dispose();throw new IOException("Cannot confirm the opened drive's hardware identity.");}

                int serialOffset=BitConverter.ToInt32(descriptor,24);

                if(target.Serial.Length>0) {

                    if(serialOffset<36 || serialOffset>=got){raw.Dispose();throw new IOException("The opened drive did not provide its expected serial. Formatting is blocked.");}

                    int end=serialOffset;while(end<got && descriptor[end]!=0)end++;

                    string serial=System.Text.Encoding.ASCII.GetString(descriptor,serialOffset,end-serialOffset).Trim();

                    if(!String.Equals(serial,target.Serial,StringComparison.OrdinalIgnoreCase)){raw.Dispose();throw new IOException("Opened-drive serial does not match the selected drive. Formatting is blocked.");}

                }

            } finally {Marshal.FreeHGlobal(query);}

            Storage.Recheck(target);

            raw.Dispose();

            disk=CreateFile(@"\\.\PhysicalDrive"+target.Number,readOnly?0x80000000u:0xC0000000u,3,IntPtr.Zero,3,0x40000000,IntPtr.Zero);

            if(disk.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot open selected drive for bounded transfers.");

            Storage.Recheck(target);

        }catch{Dispose();throw;}

    }

    public byte[] Read(long lba,int length,CancellationToken cancel) {

        long offset=checked(lba*512);

        if(length<0 || length%512!=0 || offset<0 || offset>size-length)throw new IOException("Read outside selected drive bounds.");

        var bytes=new byte[length];for(int at=0;at<length;at+=65536)Transfer(false,offset+at,bytes,at,Math.Min(65536,length-at),cancel);return bytes;

    }

    public void Write(long lba,byte[] bytes,CancellationToken cancel,bool verify=true) {

        if(readOnly)throw new IOException("This disk handle is read-only. Writes are blocked.");

        long offset=checked(lba*512);

        if(bytes.Length%512!=0 || offset<0 || offset>size-bytes.Length)throw new IOException("Write outside selected drive bounds.");

        for(int at=0;at<bytes.Length;at+=65536)Transfer(true,offset+at,bytes,at,Math.Min(65536,bytes.Length-at),cancel);

        if(verify)using(var sha=System.Security.Cryptography.SHA256.Create())written.Add(new Written{Offset=offset,Size=bytes.Length,Hash=sha.ComputeHash(bytes)});

    }

    public void Verify(Action<string,long,long> progress,System.Threading.CancellationToken cancel) {

        progress("Flushing writes before verification",0,1);

        Flush();long total=written.Sum(x=>(long)x.Size),done=0;

        byte[] bytes=new byte[written.Count==0?512:written.Max(x=>x.Size)];

        foreach(var entry in written) {

            cancel.ThrowIfCancellationRequested();

            if(entry.Offset<0 || entry.Offset>size-entry.Size || entry.Offset%512!=0 || entry.Size%512!=0)throw new IOException("Verification request outside aligned disk bounds.");

            progress("Verifying sector "+entry.Offset/512,done,total);

            for(int off=0;off<entry.Size;off+=65536)Transfer(false,entry.Offset+off,bytes,off,Math.Min(65536,entry.Size-off),cancel);

            using(var sha=System.Security.Cryptography.SHA256.Create())if(!sha.ComputeHash(bytes,0,entry.Size).SequenceEqual(entry.Hash))throw new IOException("Read-back mismatch at sector "+entry.Offset/512+". Drive setup did not complete.");

            done+=entry.Size;progress("Checking written partitions and dashboard files",done,total);

        }

    }

    public void BeginFinalTableVerification(){written.Clear();}

    public void Flush(){

        var task=System.Threading.Tasks.Task.Run(()=>{if(!FlushFileBuffers(disk))throw new Win32Exception(Marshal.GetLastWin32Error(),"Could not flush written data to the drive.");});

        if(!task.Wait(30000))throw new IOException("The storage connection timed out while flushing data. Drive setup did not complete.");

    }

    public void Dispose(){if(disk!=null)disk.Dispose();foreach(var h in locks)h.Dispose();locks.Clear();if(!ioAbandoned){if(ioSignal!=null){ioSignal.Dispose();ioSignal=null;}if(ioBuffer!=IntPtr.Zero){Marshal.FreeHGlobal(ioBuffer);ioBuffer=IntPtr.Zero;}if(ioOverlap!=IntPtr.Zero){Marshal.FreeHGlobal(ioOverlap);ioOverlap=IntPtr.Zero;}}}

}

}

