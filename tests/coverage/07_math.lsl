// 07_math — trig, sqrt, pow, log, floor/ceil/round, abs, frand, vectors
default {
    state_entry() {
        // Trig at known values
        llOwnerSay("SIN0=" + (string)((integer)(llSin(0.0) * 1000)));     // 0
        llOwnerSay("COS0=" + (string)((integer)(llCos(0.0) * 1000)));     // 1000
        llOwnerSay("TAN0=" + (string)((integer)(llTan(0.0) * 1000)));     // 0
        llOwnerSay("ASIN1=" + (string)((integer)(llAsin(1.0) * 1000)));   // PI/2 * 1000 ~ 1570
        llOwnerSay("ACOS1=" + (string)((integer)(llAcos(1.0) * 1000)));   // 0
        llOwnerSay("ATAN2=" + (string)((integer)(llAtan2(1.0, 1.0) * 1000))); // PI/4 * 1000 ~ 785
        // Power / log
        llOwnerSay("SQRT=" + (string)((integer)llSqrt(16.0)));            // 4
        llOwnerSay("POW=" + (string)((integer)llPow(2.0, 10.0)));         // 1024
        llOwnerSay("LOG=" + (string)((integer)(llLog(2.718281828) * 1000))); // ~999
        llOwnerSay("LOG10=" + (string)((integer)llLog10(1000.0)));        // 3
        // Round / floor / ceil
        llOwnerSay("FLOOR=" + (string)llFloor(3.7));                      // 3
        llOwnerSay("CEIL=" + (string)llCeil(3.2));                        // 4
        llOwnerSay("ROUND=" + (string)llRound(3.5));                      // 4
        llOwnerSay("ABS=" + (string)llAbs(-42));                          // 42
        llOwnerSay("FABS=" + (string)((integer)(llFabs(-2.5) * 10)));     // 25
        // Frand: just check it's in range [0, max)
        float fr = llFrand(10.0);
        if (fr >= 0.0 && fr < 10.0) llOwnerSay("FRAND_OK");
        else                         llOwnerSay("FRAND_BAD");
        // Vectors
        llOwnerSay("VMAG=" + (string)((integer)llVecMag(<3.0, 4.0, 0.0>)));      // 5
        vector vn = llVecNorm(<3.0, 4.0, 0.0>);
        llOwnerSay("VNX=" + (string)((integer)(vn.x * 1000)));                   // 600
        llOwnerSay("VDIST=" + (string)((integer)llVecDist(<0.0,0.0,0.0>, <3.0,4.0,0.0>))); // 5
    }
}
