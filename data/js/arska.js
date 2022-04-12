const sections = [{ "url": "/", "en": "Dashboard" }, { "url": "/inputs", "en": "Inputs" }
    , { "url": "/channels", "en": "Channels" }, { "url": "/admin", "en": "Admin" }];


// https://stackoverflow.com/questions/7317273/warn-user-before-leaving-web-page-with-unsaved-changes
var formSubmitting = false;
var setFormSubmitting = function () { formSubmitting = true; };
var isDirty = function () { return true; }


function setEnergyMeterFields(val) { //
    idx = parseInt(val);

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

}
function initChannelForm() {
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
function initUrlBar(url) {
    var headdiv = document.getElementById("headdiv");

    var h1 = document.createElement('h1');
    h1.innerHTML = "Arska Node";
    headdiv.appendChild(h1);

    sections.forEach((sect, idx) => {
        if (url == sect.url) {
            var span = document.createElement('span');
            var b = document.createElement('b');
            b.innerHTML = sect.en;
            span.appendChild(b);
            headdiv.appendChild(span);
        }
        else {
            var a = document.createElement('a');
            var link = document.createTextNode(sect.en);
            a.appendChild(link);
            a.title = sect.en;
            a.href = sect.url;
            headdiv.appendChild(a);
        }

        if (idx < sections.length - 1) {
            var sepa = document.createTextNode(" | ");
            headdiv.appendChild(sepa);
        }


    });
    // <a href="/">Dashboard</a> | <span> <b>Admin</b> </span>

}
function initForm(url) {
    initUrlBar(url);
    if (url == '/admin') {
        initWifiForm();
    }
    else if (url == '/channels') {
        initChannelForm();
    }
    var footerdiv = document.getElementById("footerdiv");
    if (footerdiv) {
        footerdiv.innerHTML = "<a href='http://netgalleria.fi/rd/?arska-wiki'>Arska Wiki</a> | <a href='http://netgalleria.fi/rd/?arska-states'>States</a> | <a href='http://netgalleria.fi/rd/?arska-rulesets'>Rulesets</a>";
    }
  
  
/* TESTING NEW FEATURES
        $.ajax({
            url: 'http://feelthenature.fi:8080/state_series?price_area=FI&states=all&location=Espoo&api_key=353f55d5-ca07-4d50-a790-2f2079c6e',
            type: "GET",
            crossDomain: true,
            success: function (data) {
                console.log(data);
            },
            error: function (error) {
                console.log(`Error ${error}`);
            }
        });
    } */
/* 
    if (window.getAttribute('beforeunload') !== 'true') {
        window.addEventListener("beforeunload", function (e) {
            if (formSubmitting || !isDirty()) {
                return undefined;
            }
            var confirmationMessage = 'It looks like you have been editing something. '
                + 'If you leave before saving, your changes will be lost.';
    
            (e || window.event).returnValue = confirmationMessage; //Gecko + IE
            return confirmationMessage; //Gecko + Webkit, Safari, Chrome etc.
        });
     }
*/

}

function compare_wifis(a, b) {
    return ((a.rssi > b.rssi) ? -1 : 1);
}

function initWifiForm() {
    var wifisp = JSON.parse(wifis);
    wifisp.sort(compare_wifis);
    var wifi_sel = document.getElementById("wifi_ssid");
    wifisp.forEach(wifi => {
        if (wifi.id) {
            var opt = document.createElement("option");
            opt.value = wifi.id;
            if (wifi.id == wifi_ssid) //from constants via template processing
                opt.selected = true;
            opt.innerHTML = wifi.id + ' (' + wifi.rssi + ')';
            wifi_sel.appendChild(opt);
        }
    })

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
function checkAdminForm() {
    document.querySelector('#ts').value = Date.now() / 1000;
    var http_password = document.querySelector('#http_password').value;
    var http_password2 = document.querySelector('#http_password2').value;
    if (http_password && http_password.length < 5) {
        alert('Password too short, minimum 5 characters.');
        return false;
    }
    if (http_password != http_password2) {
        alert('Passwords do not match!');
        return false;
    }
    return true;
}

function clearText(elem) {
    elem.value = '';
}
// Ruleset processing
function processRulesetImport(evt) {
    const fldA = evt.target.id.split("_");
    channel_idx = parseInt(fldA[1]);

    let data = evt.clipboardData.getData('text/plain');
    try {
        obj = JSON.parse(data);
        evt.target.value = 'x';
        document.getElementById(evt.target.id).value = '';
        if ((!'rulesetId' in obj) || !('rules' in obj)) {
            alert("Invalid ruleset data.");
            return;
        }
        let import_it = confirm("Do you want to import rule set over existing rules?");

    } catch (e) {
        alert('Invalid ruleset (' + e + ')'); // error in the above string (in this case, yes)!
        evt.target.value = '';
        return false;
    }

    for (let i = 0; i < CHANNELS; i++) {
        let rule_states_e = document.getElementById("st_" + channel_idx + "_" + i);
        let rule_onoff_cb = document.getElementById("ctcb_" + channel_idx + "_" + i);
        let rule_target_e = document.getElementById("t_" + channel_idx + "_" + i);
    
        if (obj["rules"].length >= i + 1) {
            rule_states_e.value = JSON.stringify(obj["rules"][i]["states"]).replace("[", "").replace("]", "");
            rule_onoff_cb.checked = obj["rules"][i]["on"];
            if (obj["rules"][i]["target"]) {
                rule_target_e.value = obj["rules"][i]["target"];
            }
            else {
                rule_target_e.value = 0;
            }    
        }
        else {
            rule_states_e.value = '';
            rule_onoff_cb.checked = false;
            rule_target_e.value = 0;

        }
    }
}
let rs1 = document.getElementById('rs1')
document.addEventListener('paste', e => {
    processRulesetImport(e);
})

