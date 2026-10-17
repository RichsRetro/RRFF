using System;
using System.IO;
using System.Linq;
using System.Text;
namespace RichRetro.Fatx {
internal static class FatxNames {
    static readonly Encoding Names=Encoding.GetEncoding(1252,EncoderFallback.ExceptionFallback,DecoderFallback.ExceptionFallback);
    public static byte[] Encode(string name){
        if(String.IsNullOrEmpty(name)||name=="."||name==".."||name.Any(c=>c<32||"\"*+,/:;<=>?\\|".IndexOf(c)>=0))throw new IOException("Invalid FATX filename character: "+name);
        byte[] bytes;try{bytes=Names.GetBytes(name);}catch(EncoderFallbackException){throw new IOException("Filename cannot be represented in the Xbox Western character encoding: "+name);}
        if(bytes.Length>42)throw new IOException("FATX filename exceeds 42 bytes; no matching automatic fix  -  see Game compatibility: "+name);
        return bytes;
    }
    public static void Validate(string name){Encode(name);}
    public static string Decode(byte[] bytes,int offset,int count){return Names.GetString(bytes,offset,count);}
}
}
