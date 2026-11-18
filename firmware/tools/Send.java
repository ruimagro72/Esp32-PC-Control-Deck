// Send one console command to the Deck and print the answer for a few seconds (JDK only).
//   java Send.java PORT "command" [seconds]
import java.io.*;

public class Send {
    public static void main(String[] a) throws Exception {
        new ProcessBuilder("stty", "-F", a[0], "115200", "raw", "-echo").inheritIO().start().waitFor();
        long wait = (a.length > 2 ? Long.parseLong(a[2]) : 3) * 1000;
        try (FileOutputStream w = new FileOutputStream(a[0]); FileInputStream r = new FileInputStream(a[0])) {
            w.write((a[1] + "\n").getBytes()); w.flush();
            long end = System.currentTimeMillis() + wait;
            byte[] buf = new byte[4096];
            while (System.currentTimeMillis() < end) {
                if (r.available() > 0) { int n = r.read(buf); System.out.write(buf, 0, n); System.out.flush(); }
                else Thread.sleep(20);
            }
        }
    }
}
