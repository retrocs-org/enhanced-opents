<?php
declare(strict_types=1);

function webSetting(string $name): string
{
    $environment = getenv($name);
    if ($environment !== false) {
        return $environment;
    }

    static $settings;
    if ($settings === null) {
        $path = __DIR__ . '/config.inc.php';
        if (is_file($path)) {
            define('OPENTS_WEB_CONFIG', true);
            $config = require $path;
            if (!is_array($config)) {
                throw new RuntimeException('config.inc.php must return a settings array.');
            }
            $settings = $config;
        } else {
            $settings = [];
        }
    }

    $value = $settings[$name] ?? '';
    if (!is_string($value)) {
        throw new RuntimeException("The website setting $name must be a string.");
    }
    return $value;
}
