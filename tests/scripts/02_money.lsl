// Send 5 L$ to a known recipient and confirm via owner-say.
key RECIP = "22222222-2222-2222-2222-222222222222";

default
{
    state_entry()
    {
        llOwnerSay("starting balance: " + (string)llGetMyAccountBalance());
        llRequestPermissions(llGetOwner(), PERMISSION_DEBIT);
    }
    run_time_permissions(integer p)
    {
        if (p & PERMISSION_DEBIT)
        {
            integer rc = llGiveMoney(RECIP, 5);
            llOwnerSay("llGiveMoney returned " + (string)rc);
            llOwnerSay("ending balance: " + (string)llGetMyAccountBalance());
        }
    }
}
