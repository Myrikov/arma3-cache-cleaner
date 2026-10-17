params [
    ["_flushIntervalBase", 900],
    ["_gcInterval", 300],
    ["_enableAdaptive", true],
    ["_pageFaultThreshold", 50000],
    ["_enableDLLCleaner", true],
    ["_enableSQFGC", true],
    ["_maxDeadBodies", 50],
    ["_maxGroundHolders", 100],
    ["_maxWrecks", 30],
    ["_craterCleanup", true],
    ["_emptyGroupCleanup", true],
    ["_deleteDistance", 500]
];

private _dllAvailable = false;
if (_enableDLLCleaner) then {
    private _ver = "cleaner_x64" callExtension "version";
    if (_ver != "") then {
        _dllAvailable = true;
    };
};

private _fnc_getPageFaults = {
    if (!_dllAvailable) exitWith { 0 };
    private _res = "cleaner_x64" callExtension "pagefaults";
    parseNumber _res
};

private _fnc_dllFlush = {
    params [["_type", "fullflush"]];
    if (!_dllAvailable) exitWith { "NO_DLL" };
    private _res = "cleaner_x64" callExtension _type;
    _res
};

private _fnc_isFarFromPlayers = {
    params ["_obj", "_dist"];
    private _far = true;
    {
        if (_obj distance2D _x < _dist) exitWith { _far = false };
    } forEach allPlayers;
    _far
};

private _fnc_sqfGC = {
    private _startTime = diag_tickTime;
    private _deletedBodies = 0;
    private _deletedHolders = 0;
    private _deletedWrecks = 0;
    private _deletedCraters = 0;
    private _deletedGroups = 0;
    private _deletedParticles = 0;

    private _allDead = allDead;
    if (count _allDead > _maxDeadBodies) then {
        private _toDelete = _allDead select [0, (count _allDead) - _maxDeadBodies];
        {
            if ([_x, _deleteDistance] call _fnc_isFarFromPlayers) then {
                deleteVehicle _x;
                _deletedBodies = _deletedBodies + 1;
            };
        } forEach _toDelete;
    };

    private _holders = allMissionObjects "GroundWeaponHolder";
    if (count _holders > _maxGroundHolders) then {
        private _toDelete = _holders select [0, (count _holders) - _maxGroundHolders];
        {
            if ([_x, _deleteDistance] call _fnc_isFarFromPlayers) then {
                deleteVehicle _x;
                _deletedHolders = _deletedHolders + 1;
            };
        } forEach _toDelete;
    };

    private _wrecks = allMissionObjects "Land_Wreck_Car_F"
        + allMissionObjects "Land_Wreck_Car2_F"
        + allMissionObjects "Land_Wreck_Car3_F"
        + allMissionObjects "Land_Wreck_Truck_dropside_F"
        + allMissionObjects "Land_Wreck_Heli_Attack_01_F"
        + allMissionObjects "Land_Wreck_Plane_Transport_01_F";

    {
        if (!alive _x && {_x isKindOf "AllVehicles"}) then {
            _wrecks pushBackUnique _x;
        };
    } forEach vehicles;

    if (count _wrecks > _maxWrecks) then {
        private _toDelete = _wrecks select [0, (count _wrecks) - _maxWrecks];
        {
            if ([_x, _deleteDistance] call _fnc_isFarFromPlayers) then {
                deleteVehicle _x;
                _deletedWrecks = _deletedWrecks + 1;
            };
        } forEach _toDelete;
    };

    if (_craterCleanup) then {
        {
            if ([_x, _deleteDistance / 2] call _fnc_isFarFromPlayers) then {
                deleteVehicle _x;
                _deletedCraters = _deletedCraters + 1;
            };
        } forEach (allMissionObjects "CraterLong");

        {
            if ([_x, _deleteDistance / 2] call _fnc_isFarFromPlayers) then {
                deleteVehicle _x;
                _deletedCraters = _deletedCraters + 1;
            };
        } forEach (allMissionObjects "CraterOnGround");

        {
            if (_x isKindOf "Crater" || {typeOf _x find "Crater" >= 0}) then {
                if ([_x, _deleteDistance / 2] call _fnc_isFarFromPlayers) then {
                    deleteVehicle _x;
                    _deletedCraters = _deletedCraters + 1;
                };
            };
        } forEach allMissionObjects "";
    };

    if (_emptyGroupCleanup) then {
        {
            if (count units _x == 0) then {
                deleteGroup _x;
                _deletedGroups = _deletedGroups + 1;
            };
        } forEach allGroups;
    };

    {
        if (!alive _x) then {
            deleteVehicle _x;
        };
    } forEach allMines;

    {
        if (_x isKindOf "SmokeShell" || {_x isKindOf "FlareBase"}) then {
            if ([_x, 200] call _fnc_isFarFromPlayers) then {
                deleteVehicle _x;
                _deletedParticles = _deletedParticles + 1;
            };
        };
    } forEach allMissionObjects "GrenadeHand";

    private _totalDeleted = _deletedBodies + _deletedHolders + _deletedWrecks
        + _deletedCraters + _deletedGroups + _deletedParticles;

    _totalDeleted
};

private _fnc_getAdaptiveInterval = {
    if (!_enableAdaptive || !_dllAvailable) exitWith { _flushIntervalBase };

    private _pf = call _fnc_getPageFaults;
    private _interval = _flushIntervalBase;

    if (_pf > _pageFaultThreshold * 3) then {
        _interval = _flushIntervalBase / 4;
    } else {
        if (_pf > _pageFaultThreshold * 2) then {
            _interval = _flushIntervalBase / 3;
        } else {
            if (_pf > _pageFaultThreshold) then {
                _interval = _flushIntervalBase / 2;
            };
        };
    };

    _interval max 60
};

if (_enableDLLCleaner && _dllAvailable) then {
    [] spawn {
        private _cycleCount = 0;
        while {true} do {
            private _interval = call _fnc_getAdaptiveInterval;
            uiSleep _interval;

            _cycleCount = _cycleCount + 1;

            ["fullflush"] call _fnc_dllFlush;

            if (_cycleCount mod 3 == 0) then {
                ["trim"] call _fnc_dllFlush;
            };
        };
    };
};

if (_enableSQFGC) then {
    [] spawn {
        uiSleep 60;
        while {true} do {
            call _fnc_sqfGC;
            uiSleep _gcInterval;
        };
    };
};

[] spawn {
    while {true} do {
        uiSleep 60;
        if (diag_fps < 15 && _dllAvailable) then {
            "cleaner_x64" callExtension "fullflush";
        };
    };
};
