<?php
declare(strict_types=1);

require_once __DIR__ . '/settings.php';

function operatorIdentity(string $credential, ?string $name = null): ?string
{
    $operator = webSetting('OPENTS_WDT_OPERATOR_NAME');
    $secret = webSetting('OPENTS_WDT_OPERATOR_TOKEN');
    if (!$operator || strlen($operator) > 80 || !$secret || strlen($secret) < 32
        || hash_equals(webSetting('OPENTS_WDT_SERVER_TOKEN'), $secret)) {
        return null;
    }
    return ($name === null || hash_equals($operator, $name)) && hash_equals($secret, $credential)
        ? $operator : null;
}
