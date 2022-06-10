/*
Copyright, Netgalleria Oy, Olli Rinne 2021-2022
*/
const sections = [{ "url": "/", "en": "Dashboard" }, { "url": "/inputs", "en": "Services" }
    , { "url": "/channels", "en": "Channels" }, { "url": "/admin", "en": "Admin" }];

const opers = JSON.parse('%OPERS%');
const variables = JSON.parse('%VARIABLES%');
const CHANNEL_COUNT = parseInt('%CHANNEL_COUNT%'); //parseInt hack to prevent automatic format to mess it up
const CHANNEL_CONDITIONS_MAX = parseInt('%CHANNEL_CONDITIONS_MAX%');
const RULE_STATEMENTS_MAX = parseInt('%RULE_STATEMENTS_MAX%');
const channels = JSON.parse('%channels%');  //moving data (for UI processing ) 
const lang = '%lang%';
const using_default_password = ('%using_default_password%' === 'true');
const DEBUG_MODE = ('%DEBUG_MODE%' === 'true');
const compile_date = '%compile_date%';

// https://stackoverflow.com/questions/7317273/warn-user-before-leaving-web-page-with-unsaved-changes
var formSubmitting = false;
var setFormSubmitting = function () {
    formSubmitting = true;
};

//disable/enable UI elements depending on varible mode (source/replica)
function setVariableMode(variable_mode) {
    document.getElementById("entsoe_api_key").disabled = (variable_mode != 0);
    document.getElementById("entsoe_area_code").disabled = (variable_mode != 0);
    document.getElementById("forecast_loc").disabled = (variable_mode != 0);

    document.getElementById("variable_server").disabled = (variable_mode != 1);
}


function statusCBClicked(elCb) {
    //if (elCb.checked) {
    setTimeout(function () { updateStatus(elCb.checked); }, 1000);
    //}
    document.getElementById("variables").style.display = document.getElementById("statusauto").checked ? "block" : "none";
}

// update variables and channels statuses to channels form
function updateStatus(show_variables = true) {
    $.getJSON('/status', function (data) {
        msgdiv = document.getElementById("msgdiv");
        keyfd = document.getElementById("keyfd");
        if (msgdiv) {
            msgDate = new Date(data.last_msg_ts * 1000);
            msgDateStr = msgDate.getFullYear() + '-' + ('0' + (msgDate.getMonth() + 1)).slice(-2) + '-' + ('0' + msgDate.getDate()).slice(-2) ;
            //last_msg_type,msgDate.toLocaleDateString() 
            msgdiv.innerHTML = '<span class="msg' + data.last_msg_type + '">' + msgDateStr + ' ' + msgDate.toLocaleTimeString()  + ' ' + data.last_msg_msg  + '</span>';            
        }   
        if (keyfd) {
            selling = data.variables["102"];
            price = data.variables["0"];
            
            emDate = new Date(data.energym_read_last * 1000);
         
            selling_text = (selling > 0) ? "Selling ⬆ " : "Buying ⬇ ";
            keyfd.innerHTML = selling_text + '<span class="big">' + Math.abs(selling) + ' W</span> (period average '+  emDate.toLocaleTimeString() + '), Price: <span class="big">' + price + ' ¢/kWh </span>';            
        }
        if (show_variables) {
            document.getElementById("variables").style.display = document.getElementById("statusauto").checked ? "block" : "none";

            $("#tblVariables_tb").empty();
            $.each(data.variables, function (i, variable) {
                var_this = getVariable(i);
                if (variable[2] == 50 || variable[2] == 51) {
                    newRow = '<tr><td>' + var_this[1] + '</td><td>' + variable ? "ON" : "OFF" + '</td></tr>';
                }
                else {
                    newRow = '<tr><td>' + var_this[1] + '</td><td>' + variable.replace('"', '').replace('"', '') + '</td></tr>';
                }
                $(newRow).appendTo($("#tblVariables_tb"));
            });
            $('<tr><td>updated</td><td>' + data.localtime.substring(11) + '</td></tr></table>').appendTo($("#tblVariables_tb"));
        }
        $.each(data.channels, function (i, channel_status) {
            show_channel_status(i, channel_status)
        });
    });
    //  if (document.getElementById('statusauto').checked) {
    setTimeout(function () { updateStatus(show_variables); }, 60000);
    //  }
}


function show_channel_status(channel_idx, is_up) {
    status_el = document.getElementById("status_" + channel_idx); //.href = is_up ? "#green" : "#red";
    href = is_up ? "#green" : "#red";
    //console.log(status_el.id + ", " + href);
    if (status_el)
        status_el.setAttributeNS('http://www.w3.org/1999/xlink', 'href', href);
 
    // snprintf(buff2,90,  "<svg viewBox='0 0 100 100' style='height:3em;'><use href='%s' id='status_%d'/></svg>",s.ch[channel_idx].is_up ? "#green" : "#red",channel_idx);
}

// before submitting input admin form
var submitInputsForm = function (e) {
    if (!confirm("Save and restart."))
        return false;
    formSubmitting = true;
    return true;
}

// Before submitting channels config form
var submitChannelForm = function (e) {
    // e.preventDefault();
    if (!confirm("Save channel settings?"))
        return false;

    // set name attributes to inputs to post (not coming from server to save space)
    let inputs = document.querySelectorAll("input[id^='ctcb_'], input[id^='t_'], input[id^='chty_']");
    console.log("inputs.length:" + inputs.length);
    for (let i = 0; i < inputs.length; i++) {
        if (inputs[i].id != inputs[i].name) {
            inputs[i].name = inputs[i].id;
        }
    }

    // statements from input fields 
    // clean first storage input values
    let stmts_s = document.querySelectorAll("input[id^='stmts_']");
    for (let i = 0; i < stmts_s.length; i++) {
        stmts_s[i].value = '[]';
        if (i == 0)
            stmts_s[i].name = stmts_s[i].id; //send at least one field even if empty
    }

    // then save new values to be saved on the server
    let stmtDivs = document.querySelectorAll("div[id^='std_']");
    if (stmtDivs && stmtDivs.length > 0) {
        for (let i = 0; i < stmtDivs.length; i++) {
            console.log("stmp element ", i);
            const divEl = stmtDivs[i];
            const fldA = divEl.id.split("_");
            const var_val = parseInt(document.getElementById(divEl.id.replace("std", "var")).value);
            const op_val = parseInt(document.getElementById(divEl.id.replace("std", "op")).value);

            if (var_val < 0 || op_val < 0)
                continue;

            //todo decimal, test  that number valid
            const const_val = parseFloat(document.getElementById(divEl.id.replace("std", "const")).value);

            saveStoreId = "stmts_" + fldA[1] + "_" + fldA[2];

            // console.log(saveStoreId + " " + JSON.stringify([var_val, op_val, const_val]));
            saveStoreEl = document.getElementById(saveStoreId);
            let stmt_list = [];
            if (saveStoreEl.value) {
                stmt_list = JSON.parse(saveStoreEl.value);
            }
            stmt_list.push([var_val, op_val, const_val]);
            saveStoreEl.value = JSON.stringify(stmt_list);

            saveStoreEl.name = saveStoreEl.id; // only fields with a name are posted 
            console.log(saveStoreEl.id, saveStoreEl.name, saveStoreEl.value);
        }
    }
    // console.log("form valid: ",$("#chFrm").valid());
    //enable before submit for posting
    let disSels = document.querySelectorAll('select[disabled="disabled"], input[disabled="disabled"]');
    for (var i = 0; i < disSels.length; i++) {
        disSels[i].disabled = false;
    }
    formSubmitting = true;
    return true;
};


var isDirty = function () { return true; }

function getVariable(variable_id) {
    for (var i = 0; i < variables.length; i++) {
        if (variables[i][0] == variable_id)
            return variables[i];
    }
    return null;
}

// Add option to a given select element
function addOption(el, value, text, selected = false) {
    var opt = document.createElement("option");
    opt.value = value;
    opt.selected = selected;
    opt.innerHTML = text
    // then append it to the select element
    el.appendChild(opt);
}



function populateStmtField(varFld, stmt = [-1, -1, 0]) {
    if (varFld.options && varFld.options.length > 0) {
        return; //already populated
    }
    fldA = varFld.id.split("_");
    channel_idx = parseInt(fldA[1]);
    cond_idx = parseInt(fldA[2]);

    document.getElementById(varFld.id.replace("var", "const")).style.display = "none";
    document.getElementById(varFld.id.replace("var", "op")).style.display = "none";

    advanced_mode = document.getElementById("mo_" + channel_idx + "_0").checked;

    addOption(varFld, -2, "remove");
    addOption(varFld, -1, "select", (stmt[0] == -1));

    for (var i = 0; i < variables.length; i++) {
        addOption(varFld, variables[i][0], variables[i][1], (stmt[0] == variables[i][0]));
    }
}




//unused?
function populateVariableSelects(field) {
    const selectEl = document.querySelectorAll("select[id^='var_']");
    for (let i = 0; i < selectEl.length; i++) {
        populateStmtField(selectEl[i]);
    }
}

function createElem(tagName, id = null, value = null, class_ = "", type = null) {
    const elem = document.createElement(tagName);
    if (id)
        elem.id = id;
    if (value)
        elem.value = value;
    if (class_) {
        for (const class_this of class_.split(" ")) {
            elem.classList.add(class_this);
        }
    }
    if (type)
        elem.type = type;
    return elem;
}

function addStmt(elBtn, channel_idx = -1, cond_idx = 1, stmt_idx = -1, stmt = [-1, -1, 0]) {
    if (channel_idx == -1) { //from button click, no other parameters
        fldA = elBtn.id.split("_");
        channel_idx = parseInt(fldA[1]);
        cond_idx = parseInt(fldA[2]);
    }
    //get next statement index if not defined
    if (stmt_idx == -1) {
        stmt_idx = 0;
        let div_to_search = "std_" + channel_idx + "_" + cond_idx;

        let stmtDivs = document.querySelectorAll("div[id^='" + div_to_search + "']");
        // console.log("div_to_search:" + div_to_search + ", found: " + stmtDivs.length);
        for (let i = 0; i < stmtDivs.length; i++) {
            fldB = stmtDivs[i].id.split("_");
            stmt_idx = Math.max(parseInt(fldB[3]), stmt_idx);
        }
        if (stmtDivs.length >= RULE_STATEMENTS_MAX) {
            alert('Max ' + RULE_STATEMENTS_MAX + ' statements allowed');
            return false;
        }

        stmt_idx++;
        console.log("New stmt_idx:" + stmt_idx);
    }

    suffix = "_" + channel_idx + "_" + cond_idx + "_" + (stmt_idx);
    console.log(suffix);

    //  lastEl = document.getElementById("std_" + channel_idx + "_" + cond_idx + "_" + stmt_idx);
    const sel_var = createElem("select", "var" + suffix, null, "fldstmt indent", null);
    sel_var.addEventListener("change", setVar);
    const sel_op = createElem("select", "op" + suffix, null, "fldstmt", null);

    sel_op.addEventListener("change", setOper);


    const inp_const = createElem("input", "const" + suffix, 0, "fldtiny fldstmt inpnum", "text");
    const div_std = createElem("div", "std" + suffix, -2147483648, "divstament", "text");
    div_std.appendChild(sel_var);
    div_std.appendChild(sel_op);
    div_std.appendChild(inp_const);

    elBtn.parentNode.insertBefore(div_std, elBtn);
    populateStmtField(document.getElementById("var" + suffix), stmt);
}



function populateTemplateSel(selEl, template_id = -1) {
    if (selEl.options && selEl.options.length > 0) {
        return; //already populated
    }
    addOption(selEl, -1, "select", false);
    $.getJSON('/data/template-list.json', function (data) {
        $.each(data, function (i, row) {
            addOption(selEl, row["id"], _ltext(row, "name"), (template_id == row["id"]));
        });
    });
}

// unused ?
function saveVal(el) {
    $.data(el, 'current', $(el).val());
}

//localised text
function _ltext(obj, prop) {
    if (obj.hasOwnProperty(prop + '_' + lang))
        return obj[prop + '_' + lang];
    else if (obj.hasOwnProperty(prop))
        return obj[prop];
    else
        return '[' + prop + ']';
}

function templateChanged(selEl) {
    const fldA = selEl.id.split("_");
    channel_idx = parseInt(fldA[1]);
    console.log(selEl.value, channel_idx);
    template_idx = selEl.value;
    url = '/data/templates?id=' + template_idx;
    console.log(url, selEl.id);
    $.getJSON(url, function (data) {
        if (template_idx == -1 && confirm('Remove template definitions')) {
            deleteStmtsUI(channel_idx);
            return true;
        }
        if (!confirm('Use template ' + _ltext(data, "name") + ' - ' + _ltext(data, "desc"))) {
            $(selEl).val($.data(selEl, 'current'));
            return false;
        }
        deleteStmtsUI(channel_idx);

        $.each(data.conditions, function (cond_idx, rule) {
            console.log("ctcb_" + channel_idx + "_" + cond_idx);
            document.getElementById("ctcb_" + channel_idx + "_" + cond_idx).checked = rule["on"];

            elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
            $.each(rule.statements, function (j, stmt) {
                console.log("stmt.values:" + JSON.stringify(stmt.values));
                stmt_obj = stmt.values;
                if (stmt.hasOwnProperty('const_prompt')) {
                    stmt_obj[2] = prompt(stmt.const_prompt, stmt_obj[2]);
                }

                addStmt(elBtn, channel_idx, cond_idx, j, stmt_obj);
                var_this = get_var_by_id(stmt.values[0]);
                populateOper(document.getElementById("op_" + channel_idx + "_" + cond_idx + "_" + j), var_this, stmt_obj);
            });

            /*
            if (rule.statements.length == 0) { // empty statement to start with
                addStmt(elBtn, channel_idx, cond_idx, 0);
                console.log("add empty stmt");
            }
            else
                console.log("rule.statements.length:",rule.statements.length);*/
        });

    });

    // fixing ui fields is delayed to get dom parsed ready?
    setTimeout(function () { fillStmtRules(channel_idx, 1); }, 1000);

    return true;
}
function deleteStmtsUI(channel_idx) {

    selector_str = "div[id^='std_" + channel_idx + "']";
    document.querySelectorAll(selector_str).forEach(e => e.remove());

    // reset up/down checkboxes
    for (let cond_idx = 0; cond_idx < CHANNEL_CONDITIONS_MAX; cond_idx++) {
        document.getElementById("ctcb_" + channel_idx + "_" + cond_idx).checked = false;
    }
}

function setRuleMode(channel_idx, rule_mode, reset, template_id) {
    if (rule_mode == 1 || true) {
        templateSelEl = document.getElementById("rts_" + channel_idx);
        populateTemplateSel(templateSelEl, template_id);
    }

    console.log("New rule mode:", rule_mode);
    $('#rd_' + channel_idx + ' select').attr('disabled', (rule_mode != 0));
    $('#rd_' + channel_idx + ' input').attr('disabled', (rule_mode != 0)); //jos ei iteroi?
    $('#rd_' + channel_idx + ' input').prop('readonly', (rule_mode != 0));

    $('#rd_' + channel_idx + ' .addstmtb').css({ "display": ((rule_mode != 0) ? "none" : "block") });
    $('#rts_' + channel_idx).css({ "display": ((rule_mode == 0) ? "none" : "block") });

    fillStmtRules(channel_idx, rule_mode);
}

function populateOper(el, var_this, stmt = [-1, -1, 0]) {
    fldA = el.id.split("_");
    channel_idx = parseInt(fldA[1]);
    cond_idx = parseInt(fldA[2]);

    rule_mode = document.getElementById("mo_" + channel_idx + "_0").checked ? 0 : 1;

    document.getElementById(el.id.replace("op", "const")).value = stmt[2];

    if (el.options.length == 0)
        addOption(el, -1, "select", (stmt[1] == -1));
    if (el.options)
        while (el.options.length > 1) {
            el.remove(1);
        }

    if (var_this) {
        for (let i = 0; i < opers.length; i++) {
            if (var_this[2] >= 50 && !opers[i][5]) //2-type
                continue;
            if (var_this[2] < 50 && opers[i][5])
                continue;
            const_id = el.id.replace("op", "const");
            //  console.log(const_id);
            el.style.display = "block";
            document.getElementById(el.id.replace("op", "const")).style.display = (opers[i][5]) ? "none" : "block";
            // if (advanced_mode || (opers[i][0] == stmt[1]))
            addOption(el, opers[i][0], opers[i][1], (opers[i][0] == stmt[1]));
        }
    }

    /*
        el.readonly = (!advanced_mode);
        document.getElementById(el.id.replace("op_", "var_")).readonly = (!advanced_mode);
        document.getElementById(el.id.replace("op_", "const_")).readonly = (!advanced_mode);
        */
    el.disabled = (rule_mode != 0);
    document.getElementById(el.id.replace("op_", "var_")).disabled = (rule_mode != 0);
    document.getElementById(el.id.replace("op_", "const_")).disabled = (rule_mode != 0);
}

function get_var_by_id(id) {
    for (var i = 0; i < variables.length; i++) {
        if (variables[i][0] == id) {
            return variables[i];
        }
    };
}
// set variable in dropdown select
function setVar(evt) {
    const el = evt.target;
    const fldA = el.id.split("_");
    channel_idx = parseInt(fldA[1]);
    cond_idx = parseInt(fldA[2]);
    stmt_idx = parseInt(fldA[3]);
    if (el.value > -1) { //variable selected
        // let var_this = variables[el.value];
        var_this = get_var_by_id(el.value);
        populateOper(document.getElementById(el.id.replace("var", "op")), var_this);
    }
    else if (el.value == -2) {
        // if (!document.getElementById("var_" + channel_idx + "_" + cond_idx + "_" + stmt_idx + 2) && stmt_idx>0) { //is last
        if (confirm("Confirm")) {
            //viimeinen olisi hyvä jättää, jos poistaa välistä niin numerot frakmentoituu
            var elem = document.getElementById(el.id.replace("var", "std"));
            elem.parentNode.removeChild(elem);
        }
        // }
    }

}

// operator select changed, show next statement fields if hidden
function setOper(evt) {
    const el = evt.target;
    if ((el.value >= 0)) {
        fldA = el.id.split("_");
        channel_idx = parseInt(fldA[1]);
        cond_idx = parseInt(fldA[2]);
        // show initially hidden rules
        if ((cond_idx + 1) < CHANNEL_CONDITIONS_MAX) {
            document.getElementById("ru_" + channel_idx + "_" + (cond_idx + 1)).style.display = "block";
        }
    }
}


function setEnergyMeterFields(emt) { //

    idx = parseInt(emt);

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

function fillStmtRules(channel_idx, rule_mode) {
    console.log("fillStmtRules", channel_idx, rule_mode);

    prev_rule_var_defined = true;
    for (let cond_idx = 0; cond_idx < CHANNEL_CONDITIONS_MAX; cond_idx++) {
        firstStmtVarId = "var_" + channel_idx + "_" + cond_idx + "_0";
        firstStmtVar = document.getElementById(firstStmtVarId);
        first_var_defined = !!firstStmtVar;



        //if (first_var_defined)
        //    first_var_defined = (firstStmtVar.value >= 0);

        show_rule = (rule_mode == 0) || first_var_defined;
        // testing hiding , hide empty rules to save space
        if (rule_mode == 0) {
            if (cond_idx == 0)
                show_rule = true;
            else {
                show_rule = prev_rule_var_defined;
            }
        }
        prev_rule_var_defined = first_var_defined;


        //just debug
        if (first_var_defined)
            console.log(firstStmtVar.id, "firstStmtVarId.value", firstStmtVar.value, "show_rule", show_rule);
        else
            console.log(firstStmtVarId, "undefined or 0");


        document.getElementById("ru_" + channel_idx + "_" + cond_idx).style.display = (show_rule ? "block" : "none");

        if (!first_var_defined && (rule_mode == 0)) {  // advanced more add at least one statement for each rule
            elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
            console.log("addStmt", channel_idx, cond_idx, 0);
            addStmt(elBtn, channel_idx, cond_idx, 0);
        }
    }
}

function initChannelForm() {
    console.log("initChannelForm");
    var chtype;

    //oli tässä
    for (var channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++) {
        //ch_t_%d_1
        console.log("channel_idx:" + channel_idx);
        //TODO: fix if more types coming
        chtype = document.getElementById('chty_' + channel_idx).value;
        setChannelFieldsByType(channel_idx, chtype);
    }

    let stmts_s = document.querySelectorAll("input[id^='stmts_']");
    console.log("stmts_s.length:" + stmts_s.length);
    for (let i = 0; i < stmts_s.length; i++) {
        if (stmts_s[i].value) {
            fldA = stmts_s[i].id.split("_");
            channel_idx = parseInt(fldA[1]);
            cond_idx = parseInt(fldA[2]);
            console.log(stmts_s[i].value);
            stmts = JSON.parse(stmts_s[i].value);
            if (stmts && stmts.length > 0) {
                console.log(stmts_s[i].id + ": " + JSON.stringify(stmts));
                for (let j = 0; j < stmts.length; j++) {
                    elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
                    addStmt(elBtn, channel_idx, cond_idx, j, stmts[j]);
                    var_this = get_var_by_id(stmts[j][0]); //vika indeksi oli 1
                    populateOper(document.getElementById("op_" + channel_idx + "_" + cond_idx + "_" + j), var_this, stmts[j]);
                }
            }
        }
    }

    //siirretty tähän populoinnin jälkeen



    for (let channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++) {
        rule_mode = channels[channel_idx]["cm"];
        template_id = channels[channel_idx]["tid"];
        console.log("rule_mode: " + rule_mode + ", template_id:" + template_id);

        if (rule_mode == 1) {
            templateSelEl = document.getElementById("rts_" + channel_idx);
            templateSelEl.value = template_id;
            console.log("templateSelEl.value:" + templateSelEl.value);
        }
        setRuleMode(channel_idx, rule_mode, false, template_id);
        //  rule_mode = channels[channel_idx]["cm"];
        // fillStmtRules(channel_idx, rule_mode);
    }


    if (document.getElementById('statusauto').checked) {
        setTimeout(function () { updateStatus(true); }, 3000);
    }

}

function setChannelFieldsByType(ch, chtype) {
    for (var t = 0; t < RULE_STATEMENTS_MAX; t++) {
        divid = 'td_' + ch + "_" + t;
        var uptimediv = document.querySelector('#d_uptimem_' + ch);
        if (chtype == 0)
            uptimediv.style.display = "none";
        else
            uptimediv.style.display = "block";
    }
}


function initUrlBar(url) {
    var headdiv = document.getElementById("headdiv");

    /*  var h1 = document.createElement('h1');
      h1.innerHTML = "Arska Node";
      headdiv.appendChild(h1);*/
    var hdspan = document.createElement('span');
    hdspan.innerHTML = "Arska<br>";
    hdspan.classList.add("cht");
    headdiv.appendChild(hdspan);

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
//

function initForm(url) {
    initUrlBar(url);
    if (url == '/admin') {
        initWifiForm();

        if (using_default_password) {
            document.getElementById("password_note").innerHTML = "Change your password - now using default password!"
        }


        //set timezone select element
        var timezone = document.getElementById("timezone_db").value;
        $('#timezone option').filter(function () {
            return this.value.indexOf(timezone) > -1;
        }).prop('selected', true);

        // set language select element
        var lang = document.getElementById("lang_db").value;
        $('#lang option').filter(function () {
            return this.value.indexOf(lang) > -1;
        }).prop('selected', true);


    }
    else if (url == '/channels') {
        initChannelForm();
    }
    else if (url == '/') {
        setTimeout(function () { updateStatus(false); }, 3000);
    }

    else if (url == '/inputs') {
        if (document.getElementById("VARIABLE_SOURCE_ENABLED").value == 0) {
            variable_mode = 1;
            document.getElementById("variable_mode_0").disabled = true;
        }
        else {
            variable_mode = document.getElementById("variable_mode_db").value;
        }

        setVariableMode(variable_mode);
        document.getElementById("variable_mode_" + variable_mode).checked = true;

        setEnergyMeterFields(document.getElementById("emt").value);

        //set forecast location select element
        var location = document.getElementById("forecast_loc_db").value;
        $('#forecast_loc option').filter(function () {
            return this.value.indexOf(location) > -1;
        }).prop('selected', true);

        //set forecast location select element
        var area_code = document.getElementById("entsoe_area_code_db").value;
        $('#entsoe_area_code option').filter(function () {
            return this.value.indexOf(area_code) > -1;
        }).prop('selected', true);


        // show influx definations only if enbaled by server
        if (document.getElementById("INFLUX_REPORT_ENABLED").value != 1) {
            document.getElementById("influx_div").style.display = "none";
        }
    }

    var footerdiv = document.getElementById("footerdiv");
    if (footerdiv) {
        // footerdiv.innerHTML = "<a href='http://netgalleria.fi/rd/?arska-wiki' target='arskaw'>Arska Wiki</a> ";
        footerdiv.innerHTML = "<br><div class='secbr'><a href='https://github.com/Netgalleria/arska-node/wiki' target='arskaw'>Arska Wiki</a> </div><div class='secbr'><i>Program version: " + compile_date + "</i></div>";
    }
}


// for sorting wifis by signal strength
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

// check admin form fields before form post 
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
    var op_test_gpio = document.getElementById("op_test_gpio");
    if (op_test_gpio.checked) {
        document.querySelector('#test_gpio').value = prompt("GPIO to test", "-1");
    }
    return true;
}
function doAction(action) {
    actiond_fld = document.getElementById("action");
    actiond_fld.value = action;
    save_form = document.getElementById("save_form");
    if (action == 'ts') {
        if (confirm('Syncronize device with workstation time?')) {
            document.querySelector('#ts').value = Date.now() / 1000;
            save_form.submit();
        }
    }
    else if (action == 'ota') {
        if (confirm('Backup configuration always before update! \nRestart in firmware update mode? ')) {
            save_form.submit();
        }
    }
    else if (action == 'reboot') {
        if (confirm('Restart the device')) {
            save_form.submit();
        }
    }
    else if (action == 'scan_wifis') {
        if (confirm('Scan WiFi networks now?')) {
            save_form.submit();
        }
    }
    else if (action == 'scan_sensors') {
        if (confirm('Scan connected temperature sensors? This can change sensor numbers, so scan only if neccessary and check sensor rules after scanning.')) {
            save_form.submit();
        }
    }
    else if (action == 'reset') {
        if (confirm('Backup configuration before reset! \nReset configuration? ')) {
            save_form.submit();
        }
    }
    else if (action == 'test_gpio') {
        document.querySelector('#test_gpio').value = prompt("GPIO to test", "-1");
        save_form.submit();
        
    }
     
        
}

// remove?
function clearText(elem) {
    elem.value = '';
}

// Ruleset processing
function processRulesetImport(evt) {
    if (!evt.target.id.startsWith("rules_"))
        return
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

    for (let i = 0; i < CHANNEL_COUNT; i++) {
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
/*
document.addEventListener('paste', e => {
    processRulesetImport(e);
})
*/
