function setEnergyMeterFields(val) { //
    idx = parseInt(val);
    if (backup_ap_mode_enabled == 1) { //show only wifi settings
        //alert("backup_ap_mode_enabled:" + backup_ap_mode_enabled);
        
        var elements = document.querySelectorAll('div');
        elements.forEach((el) => {
            el.style.display = "none";
        });
        var wifid = document.querySelector('#wifid');
        elements = wifid.querySelectorAll('div');

        wifid.style.display = "block"
        elements.forEach((el) => {
            el.style.display = "block";
        });
        return;
    }

    var emhd = document.querySelector('#emhd');
    var empd = document.querySelector('#empd');
    var emhd = document.querySelector('#emhd');
    var emidd = document.querySelector('#emidd');
    var baseld = document.querySelector('#baseld');

    if ([1, 2, 3].indexOf(idx) > -1) {
        emhd.style.display = "block";
        empd.style.display = "block";
    } else {
        emhd.style.display = "none";
        empd.style.display = "none";
    }
    if ([2, 3].indexOf(idx) > -1)
        baseld.style.display = "block";
    else
        baseld.style.display = "none";

    if ([3].indexOf(idx) > -1) {
        empd.style.display = "block";
        emidd.style.display = "block";
    } else {
        empd.style.display = "none";
        emidd.style.display = "none";
    }
    var chtype;
    for (var ch = 0; ch < CHANNELS; ch++) {
        //ch_t_%d_1
        //TODO: fix if more types coming
        chtype = document.getElementById('chty_' + ch).value;
        setChannelFieldsByType(ch, chtype);
    }
}

function setChannelFieldsByType(ch, chtype) {
    for (var t = 0; t < CHANNEL_TARGETS_MAX; t++) {
        divid = 'td_' + ch + "_" + t;
        var targetdiv = document.querySelector('#' + divid);
        var cbdiv = document.querySelector('#ctcbd_' + ch + "_" + t);
        var isTarget = (1 & chtype);
        var uptimediv = document.querySelector('#d_uptimem_' + ch);

        if (chtype == 0)
            uptimediv.style.display = "none";
        else
            uptimediv.style.display = "block";

        if (isTarget) {
            targetdiv.style.display = "block";
            cbdiv.style.display = "none";
        } else {
            targetdiv.style.display = "none";
            cbdiv.style.display = "block";

        }
    }
}


function setChannelFields(obj) {
    if (obj == null)
        return;
    const fldA = obj.id.split("_");
    ch = parseInt(fldA[1]);
    var chtype = obj.value;
    setChannelFieldsByType(ch, chtype);
}
//beforeAdminSubmit
function beforeSubmit() {
    document.querySelector('#ts').value = Date.now() / 1000;
    var http_password = document.querySelector('#http_password').value;
    var http_password2 = document.querySelector('#http_password2').value;
    if (http_password && http_password.length < 6) {
        alert('Password too short, minimum 6 characters.');
        return false;
    }
    if (http_password != http_password2) {
        alert('Passwords do not match!');
        return false;
    }
    return true;
}