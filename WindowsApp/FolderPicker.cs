using System;

using System.IO;
using System.Threading;
using System.Threading.Tasks;

using System.Runtime.InteropServices;

using System.Windows.Forms;



namespace RichRetro.Fatx {

internal static class FolderPicker {

    [ComImport,Guid("DC1C5A9C-E88A-4DDE-A5A1-60F82A20AEF7")]class FileOpenDialog {}

    [ComImport,Guid("D57C7288-D4AD-4768-BE02-9D969532D960"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]interface Dialog {

        [PreserveSig]int Show(IntPtr owner);

        void SetFileTypes(uint count,IntPtr types);void SetFileTypeIndex(uint index);void GetFileTypeIndex(out uint index);

        void Advise(IntPtr events,out uint cookie);void Unadvise(uint cookie);void SetOptions(uint options);void GetOptions(out uint options);

        void SetDefaultFolder(Item folder);void SetFolder(Item folder);void GetFolder(out Item folder);void GetCurrentSelection(out Item item);

        void SetFileName([MarshalAs(UnmanagedType.LPWStr)]string name);void GetFileName(out IntPtr name);

        void SetTitle([MarshalAs(UnmanagedType.LPWStr)]string title);void SetOkButtonLabel([MarshalAs(UnmanagedType.LPWStr)]string text);void SetFileNameLabel([MarshalAs(UnmanagedType.LPWStr)]string text);

        void GetResult(out Item item);void AddPlace(Item item,uint where);void SetDefaultExtension([MarshalAs(UnmanagedType.LPWStr)]string extension);

        void Close(int result);void SetClientGuid(ref Guid guid);void ClearClientData();void SetFilter(IntPtr filter);void GetResults(out IntPtr items);void GetSelectedItems(out IntPtr items);

    }

    [ComImport,Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]interface Item {

        void BindToHandler(IntPtr context,ref Guid handler,ref Guid iid,out IntPtr result);void GetParent(out Item parent);void GetDisplayName(uint kind,out IntPtr name);void GetAttributes(uint mask,out uint attributes);void Compare(Item other,uint hint,out int order);

    }

    [DllImport("shell32.dll",CharSet=CharSet.Unicode,PreserveSig=false)]static extern void SHCreateItemFromParsingName(string path,IntPtr context,ref Guid iid,out Item item);

    static string MemoryFile(char partition){return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RichsRetro","RRXDFormatter","last-"+partition+"-folder.txt");}

    public static Task<string> ChooseAsync(IWin32Window owner,char partition,string current,Action<string> log){
        IntPtr handle=owner.Handle;var completion=new TaskCompletionSource<string>();
        var worker=new Thread(()=>{try{completion.SetResult(ChooseCore(handle,partition,current,false,log));}catch(Exception e){completion.SetException(e);}});
        worker.IsBackground=true;worker.SetApartmentState(ApartmentState.STA);worker.Start();return completion.Task;
    }
    public static string Choose(IWin32Window owner,char partition,string current,bool contentsOnly=false){return ChooseCore(owner.Handle,partition,current,contentsOnly,null);}
    static string ChooseCore(IntPtr ownerHandle,char partition,string current,bool contentsOnly,Action<string> log){
        var timing=System.Diagnostics.Stopwatch.StartNew();Action<string> stage=s=>{if(log!=null)log("Folder picker: "+s+" | "+timing.ElapsedMilliseconds+" ms");};stage("reading remembered destination");

        string initial=current;

        if(String.IsNullOrEmpty(initial)||!Directory.Exists(initial))try{initial=File.ReadAllText(MemoryFile(partition)).Trim();}catch(IOException){}catch(UnauthorizedAccessException){}

        stage("creating Windows dialog");var dialog=(Dialog)new FileOpenDialog();Item result=null,folder=null;

        try{

            uint options;dialog.GetOptions(out options);dialog.SetOptions(options|0x20u|0x40u|0x800u|0x8u|0x02000000u);

            dialog.SetTitle(contentsOnly?L.T("Choose source  -  its CONTENTS go directly into ")+partition+":\\":L.T("Choose your ")+partition+L.T(" folder"));dialog.SetOkButtonLabel(contentsOnly?L.T("Use folder contents"):L.T("Select folder"));

            // Distinct Windows dialog identity plus an explicit per-partition last folder.

            var id=new Guid("75565516-0a9b-46e8-a744-0000000000"+(int)partition);dialog.SetClientGuid(ref id);

            stage("resolving starting folder");if(!String.IsNullOrEmpty(initial)&&Directory.Exists(initial)){

                var iid=typeof(Item).GUID;SHCreateItemFromParsingName(initial,IntPtr.Zero,ref iid,out folder);dialog.SetFolder(folder);

            }

            stage("showing dialog");int hr=dialog.Show(ownerHandle);stage("dialog returned");if(unchecked((uint)hr)==0x800704C7)return null;Marshal.ThrowExceptionForHR(hr);

            dialog.GetResult(out result);IntPtr name;result.GetDisplayName(0x80058000u,out name);

            string selected;try{selected=Marshal.PtrToStringUni(name);}finally{Marshal.FreeCoTaskMem(name);}

            if(!Directory.Exists(selected))throw new IOException(L.T("The selected folder is unavailable."));

            try{string file=MemoryFile(partition);Directory.CreateDirectory(Path.GetDirectoryName(file));File.WriteAllText(file,selected);}catch(IOException){}catch(UnauthorizedAccessException){}

            stage("destination selected");return selected;

        }finally{if(result!=null)Marshal.FinalReleaseComObject(result);if(folder!=null)Marshal.FinalReleaseComObject(folder);Marshal.FinalReleaseComObject(dialog);}

    }

}

}

