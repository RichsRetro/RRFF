using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
namespace RichRetro.Fatx {
internal sealed class ShellFileDrop:NativeWindow {
    readonly Control control;readonly Action<string[],Point> dropped;
    [DllImport("shell32.dll")]static extern void DragAcceptFiles(IntPtr window,bool accept);
    [DllImport("shell32.dll",CharSet=CharSet.Unicode)]static extern uint DragQueryFile(IntPtr drop,uint index,StringBuilder path,uint size);
    [DllImport("shell32.dll")]static extern void DragFinish(IntPtr drop);
    [DllImport("user32.dll",SetLastError=true)]static extern bool ChangeWindowMessageFilterEx(IntPtr window,uint message,uint action,IntPtr change);
    public ShellFileDrop(Control target,Action<string[],Point> callback){control=target;dropped=callback;control.HandleCreated+=delegate{Attach();};control.HandleDestroyed+=delegate{ReleaseHandle();};if(control.IsHandleCreated)Attach();}
    void Attach(){AssignHandle(control.Handle);foreach(uint message in new uint[]{0x233,0x49,0x4A})ChangeWindowMessageFilterEx(Handle,message,1,IntPtr.Zero);DragAcceptFiles(Handle,true);}
    protected override void WndProc(ref Message message){
        if(message.Msg!=0x233){base.WndProc(ref message);return;}
        string[] paths=null;try{uint count=DragQueryFile(message.WParam,uint.MaxValue,null,0);if(count==0||count>10000)return;paths=new string[count];for(uint i=0;i<count;i++){uint length=DragQueryFile(message.WParam,i,null,0);if(length==0||length>32767){paths=null;return;}var path=new StringBuilder((int)length+1);if(DragQueryFile(message.WParam,i,path,(uint)path.Capacity)!=length){paths=null;return;}paths[i]=path.ToString();}}
        finally{DragFinish(message.WParam);message.Result=IntPtr.Zero;}
        if(paths!=null)dropped(paths,Cursor.Position);
    }
}
}
